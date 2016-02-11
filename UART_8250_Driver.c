#include<linux/cdev.h>
#include<linux/fs.h>
#include<linux/device.h>
#include<linux/circ_buf.h>
#include<linux/slab.h>
#include<linux/sched.h>
#include<linux/uaccess.h>
#include<linux/interrupt.h>
#include<linux/ioport.h>
#include<linux/io.h>
#include<linux/semaphore.h>
#include<linux/workqueue.h>
#include<linux/init.h>
#include<linux/module.h>
#define UART_DEBUG 1
#include "uart_ioctl.h"

#define FIRST_MINOR 0
#define NR_DEVICES  1

#define READ_BUFF_SIZE 4096
#define WRITE_BUFF_SIZE 4096

#define NR_PORTS 8
#define BASE_ADDR	0X3F8
#define THR		(BASE_ADDR + 0)
#define RBR		(BASE_ADDR + 0)
#define DLAB_L		(BASE_ADDR + 0)

#define IER		(BASE_ADDR + 1)
#define DLAB_H	 	(BASE_ADDR + 1)


#define IIR	 	(BASE_ADDR + 2)
#define FCR	 	(BASE_ADDR + 2)

#define LCR 		(BASE_ADDR + 3)
#define MCR 		(BASE_ADDR + 4)
#define LSR 		(BASE_ADDR + 5)
#define MSR		(BASE_ADDR + 6)
#define SCR 		(BASE_SDDR + 7)
#define DRIVER_NAME "uart"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MANMAY&AKASH");

struct uart_port{

	struct cdev cdev;

	struct device *device;
	
	struct circ_buf wr_buff,rd_buff;
//	struct semaphore sem;
	wait_queue_head_t wr_waitq,rd_waitq;

	atomic_t open_cnt;
	
	unsigned int irq;
	
	unsigned int baud;
	unsigned long int tx_cnt,rx_cnt;
};

struct semaphore sem;
struct uart_port port[NR_DEVICES];
struct class *uart_class;

int buff_space(struct circ_buf *cbuff, unsigned int size)
{
	return CIRC_SPACE(cbuff->head, cbuff->tail, size);
}
int buff_cnt(struct circ_buf *cbuff, unsigned int size)
{
	return CIRC_CNT(cbuff->head, cbuff->tail, size);
}

void tx_chars(struct uart_port *port)
{
	char ch;
	
	if(down_interruptible(&sem))
	return;

	while(buff_cnt(&port->wr_buff,WRITE_BUFF_SIZE)){
	
	ch = port->wr_buff.buf[port->wr_buff.tail];
	
	while(!(inb_p(LSR) & 0x20));

	outb_p(ch, THR);

	port->tx_cnt++;
	
	port->wr_buff.tail = (port->wr_buff.tail+1) & (WRITE_BUFF_SIZE - 1);

#ifdef UART_DEBUG
	printk("Transmitting : %c\n",ch);
#endif
	}
	
	up(&sem);

	outb_p(inb_p(IER) & 0xFD, IER);

	wake_up_interruptible(&port->wr_waitq);

}

void rx_char(struct uart_port *port)
{
	char ch;
	
	if(down_interruptible(&sem))
	return;

	ch = inb_p(RBR);

	port->rx_cnt++;
#ifdef UART_DEBUG
	printk("Data recieved : %c\n", ch);
#endif

	if (buff_space(&port->rd_buff, READ_BUFF_SIZE == 0))
			return;
	
	port->rd_buff.buf[port->rd_buff.head] = ch;
	port->rd_buff.head = (port->rd_buff.head +1) & (READ_BUFF_SIZE -1);
	
	up(&sem);
	
	wake_up_interruptible(&port->rd_waitq);
}

irqreturn_t uart_isr(int irq, void *devid)
{
	struct uart_port *port = devid;
	char int_pend = inb_p(IIR);
#ifdef UART_DEBUG
	printk("In ISR\n:");
#endif
	
	if(int_pend & 0x01)
		return IRQ_NONE;

	while(!(int_pend & 0x01))
	{
	
		if(inb_p(LSR) & 0x01){
#ifdef UART_DEBUG
		printk("New data recieved interrupt\n");
#endif
	
		rx_char(port);

		}

	if(inb_p(LSR) & 0x20) {
#ifdef UART_DEBUG
		printk("Transmitter empty interrupt \n");
#endif

		tx_chars(port);
	}
	}

	return IRQ_HANDLED;

}
	
ssize_t uart_read(struct file *filp, char __user *ubuff , size_t cnt, loff_t *off)
{
	int i = 0;
	char ch;
	struct uart_port *port = filp->private_data;
	if(down_interruptible(&sem))
		return -1;
	
	while(buff_cnt(&port->rd_buff,READ_BUFF_SIZE) == 0) {
	
		up(&sem);
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if(wait_event_interruptible(port->rd_waitq,buff_cnt(&port->rd_buff, READ_BUFF_SIZE)))
		return -1;
		
		if(down_interruptible(&sem))
			return -ERESTARTSYS;
		}
		cnt= min((int)cnt, buff_cnt(&port->rd_buff, READ_BUFF_SIZE));
	
		for(i=0; i < cnt; i++)
		{
			ch = port->rd_buff.buf[port->rd_buff.tail];
			port->rd_buff.tail = (port->rd_buff.tail + 1) & (READ_BUFF_SIZE - 1);
			put_user(ch, &ubuff[i]);
#ifdef UART_DEBUG
		printk("Reading : %c\n",ch);
#endif
		}
		up(&sem);
		return i;
}

ssize_t uart_write(struct file *filp,const char __user *ubuff,size_t cnt,loff_t *off)
{
	int i = 0;
	char ch;
	struct uart_port *port = filp->private_data;
	
	if(down_interruptible(&sem))
		return -1;
	
	while(buff_space(&port->wr_buff,WRITE_BUFF_SIZE) == 0) {
		up(&sem);
		
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		
		if(wait_event_interruptible(port->wr_waitq, buff_space(&port->wr_buff,WRITE_BUFF_SIZE)))
			return -ERESTARTSYS;
		if(down_interruptible(&sem))
			return -ERESTARTSYS;
		}
		cnt = min((int)cnt,buff_space(&port->wr_buff,WRITE_BUFF_SIZE));
	
		for(i = 0; i <cnt; i++){
			
			get_user(ch, &ubuff[i]);
#ifdef UART_DEBUG
		printk("writing : %c\n",ch);
#endif
		port->wr_buff.buf[port->wr_buff.head] = ch;
		port->wr_buff.head = (port->wr_buff.head + 1) & (WRITE_BUFF_SIZE -1);
		}
		up(&sem);
		
		outb_p(inb_p(IER) | 0x02, IER);
		
		return i;
}

long uart_ioctl(struct file *filp,unsigned int cmd, unsigned long arg)
{
	long result = 0;
	struct uart_port *port = filp->private_data;
	
	
	if(down_interruptible(&sem))
		return -1;
	
	outb_p(inb_p(LCR) | 0x80,LCR);
	
	switch(cmd)
	{ 
	case BAUD_2400 :
                outb_p(0x30, DLAB_L);
                port->baud = 2400;
                break;

        case BAUD_4800 :
                outb_p(0x18, DLAB_L);
                port->baud = 4800;
                break;

        case BAUD_9600 :
                outb_p(0x0C, DLAB_L);
                port->baud = 9600;
                break;

        case BAUD_19200 :
                outb_p(0x06, DLAB_L);
                port->baud = 19200;
                break;

        case BAUD_38400 :
                outb_p(0x03, DLAB_L);
                port->baud = 38400;
                break;

        case BAUD_57600 :
                outb_p(0x02, DLAB_L);
                port->baud = 57600;
                break;

        case BAUD_115200 :
                outb_p(0x01, DLAB_L);
                port->baud = 115200;
                break;

        default :
                printk("Command Undefined");
                result = -EINVAL;
        }
	outb_p(inb_p(LCR) & 0x7F, LCR);
	
	up(&sem);

#ifdef UART_DEBUG
	printk("baud rate set to %u\n",port->baud);
#endif
	return result;

}


static int uart_open(struct inode *inode,struct file *filp)
{
	struct uart_port *port = container_of(inode->i_cdev,struct uart_port,cdev);
	
	filp->private_data = port;
	if(down_interruptible(&sem))
		return -1;
	
	atomic_inc(&port->open_cnt);
	
	if(atomic_read(&port->open_cnt) == 1)
	{
		if(request_irq(port->irq,uart_isr,IRQF_SHARED, "uart_int", port))
		{
			up(&sem);
			printk("Request for IRQ linr %u failed\n",port->irq);
			return -EBUSY;
		}
		
		outb_p(inb_p(IER) | 0x01, IER);
	}
	up(&sem);
	return 0;
}

int uart_close(struct inode *inode, struct file *filp)
{
	struct uart_port *port;
	port = filp->private_data;
	
	if(down_interruptible(&sem))
		return -1;
	if(atomic_dec_and_test(&port->open_cnt))
	{
		
		outb_p(inb_p(IER) & 0xFE, IER);
		
		free_irq(port->irq,port);
	}
	up(&sem);
	return 0;
}
struct file_operations uart_fops = {
	.open		=uart_open,
	.release	=uart_close,
	.write		=uart_write,
	.read		=uart_read,
	.unlocked_ioctl =uart_ioctl,
};

int __init port_init(struct uart_port *port)
{
	struct resource *tmp_resource;
	
	port->irq = 4;
	
	port->baud = 115200;
	
	tmp_resource = request_region(BASE_ADDR, NR_PORTS, "serial Port");
	if(!tmp_resource)
	{
		printk("Failed to get I/O ports\n");
		return -EBUSY;
	}
	
	outb_p(0x80, LCR);
	outb_p(0x01, DLAB_L);
	outb_p(0x00, DLAB_H);
	outb_p(0x03, LCR);
	outb_p(0x07,FCR);
	outb_p(0x0b,MCR);
	return 0;
}
dev_t dev_num;
static int __init uart_init(void)
{
	int i,result;
	dev_t temp_dev;
	
	result = alloc_chrdev_region(&dev_num,FIRST_MINOR,NR_DEVICES, "serial_driver");
	
	if(result)
	{
		printk("char driver allocationfailed\n");
		return result;
	}
	
	uart_class = class_create(THIS_MODULE, "uart");
	if(!uart_class)
	{
		printk("class creation failed\n");
		result = -EINVAL;
		goto class_fail;
	}
	
	for(i = 0; i<NR_DEVICES; i++)
	{
	
		temp_dev = MKDEV(MAJOR(dev_num),FIRST_MINOR + i);
		cdev_init(&port[i].cdev,&uart_fops);
		result = cdev_add(&port[i].cdev,temp_dev, 1);
		if(result)
		{
			printk("Cdev creation failed\n");
			goto cdev_fail;
		}
	
		port[i].device = device_create(uart_class,NULL,temp_dev, &port[i], "uart%d",i);
		if(!port[i].device)
		{	
			printk("Failed to create the device\n");
			result = -EINVAL;
			goto device_fail;
		}
		
		port[i].wr_buff.buf = kzalloc(WRITE_BUFF_SIZE,GFP_KERNEL);
		if(!port[i].wr_buff.buf)
		{
			printk("Failed to allocate memory for write buffer\n");
			result = -ENOMEM;
			goto wr_fail;
		}
		port[i].rd_buff.buf = kzalloc(READ_BUFF_SIZE,GFP_KERNEL);
		if(!port[i].rd_buff.buf)
		{
			printk("Falied to allocate memory for write buffer\n");
			result = -ENOMEM;
			goto rd_fail;
		}
	
		init_waitqueue_head(&port[i].wr_waitq);
		init_waitqueue_head(&port[i].rd_waitq);
		
		sema_init(&sem,1);
		
		port[i].open_cnt = (atomic_t)ATOMIC_INIT(0);
	}
	
	result = port_init(&port[0]);
	if(result)
		goto ports_fail;
	
	printk("serial Driver Initialised\n");
	
	return 0;
	
	ports_fail:release_region(BASE_ADDR, NR_PORTS);
	rd_fail:kfree(port[i].rd_buff.buf);
	wr_fail:kfree(port[i].wr_buff.buf);
	device_fail:device_destroy(uart_class,temp_dev);
	cdev_fail:cdev_del(&port[i].cdev);
	class_fail:class_destroy(uart_class);
	
	return result;
}

static void __exit uart_exit(void)
{
	int i;
	dev_t temp_dev;
	
	release_region(BASE_ADDR, NR_PORTS);
	
	for(i =0; i <NR_DEVICES; i++)
	{
		kfree(port[i].wr_buff.buf);
		kfree(port[i].rd_buff.buf);
		
		temp_dev = MKDEV(MAJOR(dev_num),FIRST_MINOR + i);
	
		device_destroy(uart_class, temp_dev);
		cdev_del(&port[i].cdev);
	}
		
	class_destroy(uart_class);
	
	unregister_chrdev_region(dev_num,NR_DEVICES);
	
	printk("serial Driver Removed \n");
}
module_init(uart_init)
module_exit(uart_exit)





