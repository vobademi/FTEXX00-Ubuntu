/*
 *  License-Identifier: GPL-2.0  
 *  Driver for Focaltech Fingerprint over SPI
 *  Copyright (c) 2024 Focaltech Systems (ShenZhen) Co., Ltd.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/unaligned.h>

#define INIT_SUCCESS 0x55AA

#define SPI_READ_ONLY	0x5A
#define SPI_READ_WRITE	0xA5
#define SPI_BACK_DATA	0xB9

#define MAX_BUFF_SIZE (32*1024)

#define TAG "focal "
#define LOGD(format,...)	log_printf(TAG format, ##__VA_ARGS__)

#define VERSION "v1.0.3"

typedef enum focal_wake_event_et{
	FOCAL_WAKE_EVENT_NONE=0,
	FOCAL_WAKE_EVENT_ENABLE,
	FOCAL_WAKE_EVENT_INT,
	FOCAL_WAKE_EVENT_RESUME,
	FOCAL_WAKE_EVENT_SUSPEND,
	FOCAL_WAKE_EVENT_DISABLE,
}focal_wake_event_t;

typedef enum focal_ioctl_cmd_et{
	FF_IOC_RESET_DEV=0x8086,
	FF_IOC_POWER_OFF,
	FF_IOC_POWER_ON,
	FF_IOC_IRQ_EN,
	FF_IOC_LOG_EN,
	FF_IOC_RELEASE_POLL,
	FF_IOC_CSn,
}focal_ioctl_cmd_t;

typedef struct __attribute__((__packed__)){
	uint8_t type;
	uint16_t tx_len;
	uint16_t rx_len;
	uint16_t txbuff[0];
}focal_spi_read_buff_t;

struct focal_fp_data{
	int init;
	struct spi_device *spi;
	struct gpio_desc *gpiod_rst;
	struct input_dev *input_dev;
	u8 wr_buf[MAX_BUFF_SIZE]	____cacheline_aligned;
	u8 rd_buf[MAX_BUFF_SIZE]	____cacheline_aligned;
	u8 sensor_init_data[MAX_BUFF_SIZE]	____cacheline_aligned;//save init data from sensor
};

typedef struct{
	struct miscdevice miscdev;
	struct focal_fp_data *fp_data;
} ff_ctl_context_t;

static DECLARE_WAIT_QUEUE_HEAD(focal_poll_wq);
static int g_log_enable=0;
static int focal_work_flag = FOCAL_WAKE_EVENT_DISABLE;
static int irq_is_disabled=0;

static void log_printf(const char *fmt, ...)
{
	char buf[512];
	unsigned int n = 0;

	if (g_log_enable==0) {
		return;
	}

	va_list ap;
	va_start(ap, fmt);
	memset(buf,0, 512);
	n=vsnprintf(buf, 511, fmt, ap);
	va_end(ap);

	n&=0x1ff;//n<512?n:511;
	buf[n]=0;

	printk(buf);
}


static ssize_t focal_spi_read(struct focal_fp_data *fp_data,uint16_t tx_len,uint16_t rx_len)
{
	int ret=0;
	uint8_t *tx_buf;

	if(!fp_data){
		LOGD("fp_data is null");
		return -1;
	}

	if(fp_data->init!=INIT_SUCCESS){
		LOGD("fp_data is not init");
		return -2;
	}

	struct spi_device *spi = fp_data->spi;

	if(!spi){
		LOGD("spi is null");
		return -3;
	}
	

	if(tx_len>0){
		tx_buf=fp_data->wr_buf;	
		ret=spi_write_then_read(spi,tx_buf, tx_len, fp_data->rd_buf, rx_len);
		return ret;
	}

	return spi_read(spi, fp_data->rd_buf, rx_len);
}

static irqreturn_t focal_spi_irq_handler(int irq, void *dev_id)
{
	//struct focal_fp_data *data = dev_id;
	LOGD("irq [%d] enter ",irq);
	if(focal_work_flag<=FOCAL_WAKE_EVENT_ENABLE){
		focal_work_flag = FOCAL_WAKE_EVENT_INT;
		wake_up(&focal_poll_wq);
	}

	return IRQ_HANDLED;
}

static void focal_spi_power_off(struct focal_fp_data *data)
{
	gpiod_set_value(data->gpiod_rst, 0);
}

static void focal_spi_power_on(struct focal_fp_data *data)
{
	gpiod_set_value(data->gpiod_rst, 1);
}

static void focal_spi_reset(struct focal_fp_data *data)
{
	gpiod_set_value(data->gpiod_rst, 0);
	msleep(10);
	gpiod_set_value(data->gpiod_rst, 1);
}

static void focal_spi_hw_reset(struct focal_fp_data *data) { /* If no reset GPIO provided, nothing to do */ if (!data || !data->gpiod_rst) return;
	/*
 * The gpiod API is logical: 1 == active, 0 == inactive.
 * This respects GPIO_ACTIVE_LOW from ACPI/DT automatically.
 * Do a clean reset pulse then leave the sensor out of reset.
 */
/* Ensure inactive first */
gpiod_set_value_cansleep(data->gpiod_rst, 0);
usleep_range(5000, 6000);
/* Assert reset */
gpiod_set_value_cansleep(data->gpiod_rst, 1);
usleep_range(5000, 6000);
/* Deassert reset and wait for the sensor to come up */
gpiod_set_value_cansleep(data->gpiod_rst, 0);
msleep(50);
}

static int focal_spi_configure_spi(struct focal_fp_data *data) { int ret; struct spi_device *spi = data->spi;
	/* Conservative defaults; adjust per datasheet if needed */
spi->mode = SPI_MODE_0;          /* many FocalTech parts use mode 0 */
spi->bits_per_word = 8;
if (!spi->max_speed_hz || spi->max_speed_hz > 1000000)
	spi->max_speed_hz = 1000000;  /* start at 1 MHz */
ret = spi_setup(spi);
if (ret)
	dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
else
	dev_dbg(&spi->dev, "SPI set: mode=%u bpw=%u hz=%u\n",
		spi->mode, spi->bits_per_word, spi->max_speed_hz);
return ret;
}


static int focal_spi_get_gpio_config(struct focal_fp_data *data) { int error; struct device *dev = &data->spi->dev; const char *which = NULL;
	/* Prefer a properly named reset GPIO ("reset-gpios" in DT/ACPI _DSD) */
data->gpiod_rst = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
if (IS_ERR(data->gpiod_rst)) {
	error = PTR_ERR(data->gpiod_rst);
	if (error != -EPROBE_DEFER)
		dev_err(dev, "Failed to get reset GPIO: %d\n", error);
	return error;
}
if (data->gpiod_rst)
	which = "reset";
/* Legacy fallbacks */
if (!data->gpiod_rst) {
	data->gpiod_rst = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_rst)) {
		error = PTR_ERR(data->gpiod_rst);
		if (error != -EPROBE_DEFER)
			dev_err(dev, "Failed to get power GPIO: %d\n", error);
		return error;
	}
	if (data->gpiod_rst)
		which = "power";
}
if (!data->gpiod_rst) {
	data->gpiod_rst = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_rst)) {
		error = PTR_ERR(data->gpiod_rst);
		if (error != -EPROBE_DEFER)
			dev_err(dev, "Failed to get enable GPIO: %d\n", error);
		return error;
	}
	if (data->gpiod_rst)
		which = "enable";
}
if (!data->gpiod_rst) {
	dev_dbg(dev, "No reset/power GPIO provided\n");
	return 0;
}
/* Ensure the line is inactive at request time */
gpiod_set_value_cansleep(data->gpiod_rst, 0);
dev_dbg(dev, "Using %s GPIO for reset/power (default inactive)\n", which);
return 0;
}


static int focal_spi_create_touch_input(struct focal_fp_data *data)
{
	struct input_dev *input;
	int error;

	input = devm_input_allocate_device(&data->spi->dev);
	if (!input)
		return -ENOMEM;

	data->input_dev = input;

	input->name = "focal SPI fingerint";
	input->id.bustype = BUS_SPI;
	input->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY);
	set_bit(KEY_F11, input->keybit); 
	set_bit(KEY_F12, input->keybit);

	input_set_capability(input, EV_KEY, KEY_POWER);

	error = input_register_device(input);
	if (error) {
		dev_err(&data->spi->dev,"Failed to register input device: %d", error);
		return error;
	}

	return 0;
}

static ssize_t spidev_read(struct file *filp, char __user *pUserBuf, size_t count, loff_t *f_pos)
{
	ssize_t status=0;
	unsigned short rx_len=0;
	unsigned short tx_len=0;

	struct miscdevice *dev = (struct miscdevice *)filp->private_data;
	ff_ctl_context_t *ctx = container_of(dev, ff_ctl_context_t, miscdev);

	LOGD("read count=0x%d",count);

	if(count>MAX_BUFF_SIZE){
		LOGD("read buff too long");
		return 0;
	}

	focal_spi_read_buff_t *spi_buf=NULL;

	if(count>=sizeof(focal_spi_read_buff_t)){
		status=copy_from_user(ctx->fp_data->wr_buf, pUserBuf,count);
		if(status){
			LOGD("copy_from_user err");	
			return 0;
		}
		spi_buf=(focal_spi_read_buff_t*)ctx->fp_data->wr_buf;
	}else{
		LOGD("read buff too short");
		return 0;
	}

	if(spi_buf){

		if(spi_buf->type==SPI_BACK_DATA){
			status=copy_to_user(pUserBuf,ctx->fp_data->sensor_init_data,count);
			if(status){
				LOGD("copy_to_user err");
				return 0;
			}
			return count;
		}

		if(spi_buf->type==SPI_READ_WRITE){
			tx_len=spi_buf->tx_len;
			rx_len=spi_buf->rx_len;
		}

		if(spi_buf->type==SPI_READ_ONLY){
			rx_len=spi_buf->rx_len;
			if(spi_buf->tx_len!=0){
				LOGD("read only,but tx len=%d",spi_buf->tx_len);
				return 0;
			}
		}

		if((tx_len>MAX_BUFF_SIZE)\
			||(rx_len>MAX_BUFF_SIZE)\
			||(rx_len==0)\
			||(tx_len+rx_len>MAX_BUFF_SIZE)){

			LOGD("read write len error,rxlen=%d,tx_len=%d\n",rx_len,tx_len);
			return 0;
		}

	}

	LOGD("rxlen=%d,tx_len=%d\n",rx_len,tx_len);

	if(tx_len>0){
		memcpy(ctx->fp_data->wr_buf,spi_buf->txbuff,tx_len);
	}

	status=focal_spi_read(ctx->fp_data, tx_len,rx_len);

	status=copy_to_user(pUserBuf,ctx->fp_data->rd_buf,rx_len);
	if(status){
		LOGD("copy_to_user err");
		return 0;
	}

	if(status>=0){
		return count;
	}

	return status;
}

static ssize_t spidev_write(struct file *filp,const char __user *pUserBuf,size_t count,loff_t *f_pos)
{
	ssize_t status=0;

	struct miscdevice *dev = (struct miscdevice *)filp->private_data;
	ff_ctl_context_t *ctx = container_of(dev,ff_ctl_context_t,miscdev);

	LOGD("write count=%d",count);

	if(count>MAX_BUFF_SIZE){
		LOGD("write buff too long");
		return 0;
	}

	if(count>=sizeof(focal_spi_read_buff_t)){
		status=copy_from_user(ctx->fp_data->wr_buf,pUserBuf,count);
		if(status){
			LOGD("copy_from_user err");	
			return 0;
		}	
	}else{
		LOGD("write buff too short");
		return 0;
	}

	focal_spi_read_buff_t *spi_buf=(focal_spi_read_buff_t*)ctx->fp_data->wr_buf;
	if(spi_buf->type==SPI_BACK_DATA){
		memcpy(ctx->fp_data->sensor_init_data,ctx->fp_data->wr_buf,count);
		return count;
	}

	status=spi_write(ctx->fp_data->spi,ctx->fp_data->wr_buf+sizeof(focal_spi_read_buff_t),count-sizeof(focal_spi_read_buff_t));

	if(status>=0){
		return count;
	}

	return status;
}

static int spidev_open(struct inode *inode,struct file *filp)
{
	ssize_t status=0;
	struct miscdevice *dev = (struct miscdevice *)filp->private_data;
	ff_ctl_context_t *ctx = container_of(dev, ff_ctl_context_t, miscdev);
	LOGD("%s enter",__func__);

	if(ctx){
		if(ctx->fp_data){
			LOGD("[%s]ctx->fp_data->init=0x%X",__func__,ctx->fp_data->init);
		}
	}else{
		return -1;
	}

	return status;
}

static int spidev_close(struct inode *inode, struct file *filp)
{
	ssize_t status=0;
	struct miscdevice *dev = (struct miscdevice *)filp->private_data;
   	ff_ctl_context_t *ctx = container_of(dev, ff_ctl_context_t, miscdev);

	LOGD("%s enter ",__func__);

	if(ctx){
		if(ctx->fp_data){
			if(ctx->fp_data->init==INIT_SUCCESS){	
				//devm_kfree(ctx->fp_data);
				//ctx->fp_data=NULL;
			}
		}
	}

	return status;
}
static long ff_ctl_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	int err = 0;
	struct miscdevice *dev = (struct miscdevice *)filp->private_data;
	ff_ctl_context_t *ctx = container_of(dev, ff_ctl_context_t, miscdev);
	LOGD("'%s' enter.cmd=0x%x,arg=%d", __func__,cmd,arg);

	if(!ctx){
		LOGD("ff_ctl_context is null");
		return -1;
	}

	if(!ctx->fp_data){
		LOGD("fp_data is null");
		return -2;
	}

	switch (cmd){
		
		case FF_IOC_RESET_DEV:
			focal_spi_reset(ctx->fp_data);break;
		case FF_IOC_POWER_OFF: 
			focal_spi_power_off(ctx->fp_data);break;
		case FF_IOC_POWER_ON: 
			focal_spi_power_on(ctx->fp_data);break;
		case FF_IOC_LOG_EN:
			g_log_enable=(int)arg;
			LOGD("FF_IOC_LOG_EN ,arg=%d",arg);break;
		case FF_IOC_IRQ_EN:

			if(irq_is_disabled==1&&arg!=0){
				irq_is_disabled=0;
				enable_irq(ctx->fp_data->spi->irq);
			}

			if(irq_is_disabled==0&&arg==0){
				irq_is_disabled=1;
				disable_irq(ctx->fp_data->spi->irq);
			}

			break;
		case FF_IOC_RELEASE_POLL:
			focal_work_flag = arg;
			if(arg){//arg ==0,start
				wake_up(&focal_poll_wq);
			}
			break;
		case FF_IOC_CSn:
			//focal_spi_cs_ctl(ctx->fp_data,arg);
			break;
		default:break;
	}
	
	return err;
}

#ifdef CONFIG_COMPAT
static long ff_ctl_compat_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	int err = 0;
	LOGD("focal '%s' enter.\n", __func__);

	err = ff_ctl_ioctl(filp, cmd, arg);

	return err;
}
#endif
static unsigned int focal_poll(struct file *file,poll_table *wait)
{
	int ret=0;
	LOGD("%s enter\n",__func__);

	wait_event_interruptible(focal_poll_wq, focal_work_flag > FOCAL_WAKE_EVENT_NONE);
	
	if(focal_work_flag > FOCAL_WAKE_EVENT_NONE)
		ret = focal_work_flag;

	focal_work_flag = FOCAL_WAKE_EVENT_NONE;
	return ret;
}
static struct file_operations ff_ctl_fops = {
	.owner			=	THIS_MODULE,
	.read			=	spidev_read,
	.write			=	spidev_write,
	.unlocked_ioctl	=	ff_ctl_ioctl,
	.poll			=	focal_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl	=	ff_ctl_compat_ioctl,
#endif
	.open			=	spidev_open,
	.release		=	spidev_close,
};

static ff_ctl_context_t ff_ctl_context = {
	.miscdev = {
		.minor	=	MISC_DYNAMIC_MINOR,
		.name	=	"focal_moh_spi",
		.fops	=	&ff_ctl_fops,
	}, 0,
};

static int focal_spi_probe(struct spi_device *spi) { struct focal_fp_data *fp_data; int error;
	LOGD("%s enter\n", __func__);
fp_data = devm_kzalloc(&spi->dev, sizeof(*fp_data), GFP_KERNEL);
if (!fp_data)
	return -ENOMEM;
fp_data->init = -1;
fp_data->spi = spi;
spi_set_drvdata(spi, fp_data);
/* Get reset/power GPIOs (respects ACTIVE_LOW from firmware) */
error = focal_spi_get_gpio_config(fp_data);
if (error)
	return error;
/* Set safe SPI defaults and apply to controller */
error = focal_spi_configure_spi(fp_data);
if (error)
	return error;
/* Ensure the sensor is cleanly reset and then out of reset */
focal_spi_hw_reset(fp_data);
error = focal_spi_create_touch_input(fp_data);
if (error)
	return error;
if (spi->irq) {
	error = devm_request_threaded_irq(&spi->dev, spi->irq,
					  NULL, focal_spi_irq_handler,
					  IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					  "focal-irq", fp_data);
	if (error)
		return error;
	disable_irq(spi->irq);
	irq_is_disabled = 1;
} else {
	dev_warn(&spi->dev, "No IRQ configured; continuing without interrupts\n");
}
fp_data->init = INIT_SUCCESS;
ff_ctl_context.fp_data = fp_data;
return 0;
}

static int focal_spi_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct focal_fp_data *data = spi_get_drvdata(spi);

	LOGD("%s enter ",__func__);
	if(irq_is_disabled==0){
		irq_is_disabled=1;
		disable_irq(data->spi->irq);
	}
	if(focal_work_flag<=FOCAL_WAKE_EVENT_ENABLE){
		focal_work_flag = FOCAL_WAKE_EVENT_SUSPEND;
		wake_up(&focal_poll_wq);
	}

	//focal_spi_power(data, false);

	return 0;
}

static int focal_spi_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct focal_fp_data *data = spi_get_drvdata(spi);
	LOGD("%s enter ",__func__);
	//focal_spi_power(data, true);
	if(irq_is_disabled==1){
		irq_is_disabled=0;
		enable_irq(data->spi->irq);
	}
	if(focal_work_flag<=FOCAL_WAKE_EVENT_ENABLE){
		focal_work_flag = FOCAL_WAKE_EVENT_RESUME;
		wake_up(&focal_poll_wq);
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(focal_spi_pm_ops,focal_spi_suspend,focal_spi_resume);

#ifdef CONFIG_ACPI
static const struct acpi_device_id focal_spi_acpi_match[] = {
	{ "FTE3600", 0 },
	{ "FTE4800", 0 },
	{ "FTE6600", 0 },
	{ "FTE6900", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, focal_spi_acpi_match);
#endif

static struct spi_driver focal_spi_driver = {
	.driver = {
		.name	= "focalfp-spi",
		.acpi_match_table = ACPI_PTR(focal_spi_acpi_match),
		.pm = pm_sleep_ptr(&focal_spi_pm_ops),
	},
	.probe = focal_spi_probe,
};

static int __init focal_fp_driver_init(void)
{
	int err;
	g_log_enable=1;

	LOGD("%s is called,driver version is %s\n", __func__,VERSION);

	err = misc_register(&ff_ctl_context.miscdev);
	if (err) {
		LOGD("misc_register(..) = %d.", err);
		return err;
	}

	if (spi_register_driver(&focal_spi_driver) != 0){
		LOGD("unable to add spi driver.\n");
		return 0;
	}

	g_log_enable=0;

	return 0;
}

/* should never be called */
static void __exit focal_fp_driver_exit(void)
{
	//input_free_device(ff_ctl_context.fp_data->input_dev);

	misc_deregister(&ff_ctl_context.miscdev);
	spi_unregister_driver(&focal_spi_driver);

	//devm_kfree(
	return;
}

module_init(focal_fp_driver_init);
module_exit(focal_fp_driver_exit);

//module_spi_driver(focal_spi_driver);

MODULE_AUTHOR("zhangpiaoxiang <zhangpiaoxiang@focaltech-electronics.com>");
MODULE_DESCRIPTION("Focaltech PC-SPI fingerprint driver");
MODULE_LICENSE("GPL v2");
