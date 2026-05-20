#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define LCD_ADDR 0x27
#define LCD_CHR 1
#define LCD_CMD 0
#define LINE1 0x80
#define LINE2 0xC0
#define ENABLE 0b00000100
#define BACKLIGHT 0x08

int fd;

void lcdToggleEnable(int bits) {
	usleep(500);
	write(fd, &(unsigned char){bits | ENABLE}, 1);
	usleep(500);
	write(fd, &(unsigned char){bits & ~ENABLE}, 1);
	usleep(500);
}

void lcdByte(int bits, int mode) {
	int high = mode | (bits & 0xF0) | BACKLIGHT;
	int low = mode | ((bits << 4) & 0xF0) | BACKLIGHT;
	
	write(fd, &(unsigned char){high}, 1);
	lcdToggleEnable(high);
	
	write(fd, &(unsigned char){low}, 1);
	lcdToggleEnable(low);
}

void lcdInit() {
	lcdByte(0x33, LCD_CMD);
	lcdByte(0x32, LCD_CMD);
	lcdByte(0x06, LCD_CMD);
	lcdByte(0x0C, LCD_CMD);
	lcdByte(0x28, LCD_CMD);
	lcdByte(0x01, LCD_CMD);
	usleep(5000);
}

void lcdString(const char *msg, int line) {
	lcdByte(line, LCD_CMD);
	
	char buffer[17];
	snprintf(buffer, sizeof(buffer), "%-16.16s", msg);
	
	for (int i = 0; i < 16; i++) {
		lcdByte(buffer[i], LCD_CHR);
	}
}

float getTemp() {
	FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
	if (fp == NULL) {
		perror("Temp read failed");
		return -1.0;
	}
	
	int tempMilli = 0;
	if (fscanf(fp, "%d", &tempMilli) != 1) {
		fclose(fp);
		return -1.0;	
	}
	
	fclose(fp);
	return tempMilli / 1000.0;
}

double getCpuUsage() {
	static long long lastIdle = 0, lastTotal = 0;
	
	FILE *fp = fopen("/proc/stat", "r");
	if (!fp) return 0;
	
	char cpu[5];
	long long user, nice, system, idle, iowait, irq, softirq, steal;
	
	fscanf(fp, "%s %lld %lld %lld %lld %lld %lld %lld %lld",
		cpu, &user, &nice, &system, &idle, &iowait,
		&irq, &softirq, &steal);
		
	fclose(fp);
	
	long long idleAll = idle + iowait;
	long long total = user + nice + system + idle + iowait + irq + softirq + steal;
	
	long long diffIdle = idleAll - lastIdle;
	long long diffTotal = total - lastTotal;
	
	lastIdle = idleAll;
	lastTotal = total;
	
	if (diffTotal == 0) return 0;
	
	return (100.0 * (diffTotal - diffIdle)) / diffTotal;
}

void cleanup(int sig) {
	lcdByte(0x01, LCD_CMD);
	usleep(5000);
	close(fd);
	exit(0);
}

int main() {
	fd = open("/dev/i2c-1", O_RDWR);
	if (fd < 0) {
		perror("Could not open I2C");
		return 1;
	}
	
	if (ioctl(fd, I2C_SLAVE, LCD_ADDR) < 0) {
		perror("Could not connect to LCD");
		return 1;
	}
	
	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);
	
	lcdInit();
	
	getCpuUsage();
	sleep(1);
	
	while (1) {
		float temp = getTemp();
		double cpu = getCpuUsage();
		
		char line1[17];
		char line2[17];
		
		snprintf(line1, sizeof(line1), "CPU: %.1f%%", cpu);
		snprintf(line2, sizeof(line2), "Temp: %.1f C", temp);
		
		lcdString(line1, LINE1);
		lcdString(line2, LINE2);
		
		sleep(1);
	}
	
	cleanup(0);
	return 0;
}
