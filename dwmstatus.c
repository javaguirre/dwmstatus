#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

char *tzberlin = "Europe/Amsterdam";

//Colours
const char *red = "\x1b[38;5;196m";
const char *green = "\x1b[38;5;40m";
const char *yellow = "\x1b[38;5;226m";
const char *blue = "\x1b[38;5;21m";
const char *grey = "\x1b[38;5;246m";
const char *reset = "\x1b[0m";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

char *
readfile(char *base, char *file)
{
    char *path, line[513];
    FILE *fd;

    memset(line, 0, sizeof(line));

    path = smprintf("%s/%s", base, file);
    fd = fopen(path, "r");
    if (fd == NULL)
        return NULL;
    free(path);

    if (fgets(line, sizeof(line)-1, fd) == NULL)
        return NULL;
    fclose(fd);

    return smprintf("%s", line);
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

//NET USAGE
int
parse_netdev(unsigned long long int *receivedabs, unsigned long long int *sentabs)
{
    char *buf;
    char *wlan0start;
    static int bufsize;
    FILE *devfd;

    buf = (char *) calloc(255, 1);
    bufsize = 255;
    devfd = fopen("/proc/net/dev", "r");

    // ignore the first two lines of the file
    fgets(buf, bufsize, devfd);
    fgets(buf, bufsize, devfd);

    while (fgets(buf, bufsize, devfd)) {
        if ((wlan0start = strstr(buf, "wlan0:")) != NULL) {

        // With thanks to the conky project at http://conky.sourceforge.net/
        sscanf(wlan0start + 6, "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",\
               receivedabs, sentabs);
        fclose(devfd);
        free(buf);
        return 0;
        }
    }
    fclose(devfd);
    free(buf);
    return 1;
}

char *
get_netusage()
{
    unsigned long long int oldrec, oldsent, newrec, newsent;
    double downspeed, upspeed;
    char *downspeedstr, *upspeedstr;
    char *retstr;
    int retval;

    downspeedstr = (char *) malloc(15);
    upspeedstr = (char *) malloc(15);
    retstr = (char *) malloc(42);

    retval = parse_netdev(&oldrec, &oldsent);
    if (retval) {
        fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
        exit(1);
    }

    sleep(1);
    retval = parse_netdev(&newrec, &newsent);
    if (retval) {
        fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
        exit(1);
    }

    downspeed = (newrec - oldrec) / 1024.0;
    if (downspeed > 1024.0) {
        downspeed /= 1024.0;
        sprintf(downspeedstr, "%.3f MB/s", downspeed);
    } else {
        sprintf(downspeedstr, "%.2f KB/s", downspeed);
    }

    upspeed = (newsent - oldsent) / 1024.0;
    if (upspeed > 1024.0) {
        upspeed /= 1024.0;
        sprintf(upspeedstr, "%.3f MB/s", upspeed);
    } else {
        sprintf(upspeedstr, "%.2f KB/s", upspeed);
    }
    sprintf(retstr, "down: %s up: %s", downspeedstr, upspeedstr);

    free(downspeedstr);
    free(upspeedstr);
    return retstr;
}
//END NET USAGE

//BATTERY
/*
 * Linux seems to change the filenames after suspend/hibernate
 * according to a random scheme. So just check for both possibilities.
 */
char *
getbattery(char *base)
{
    char *co;
    int descap, remcap;
    float bat;
    char stat[12];

    descap = -1;
    remcap = -1;

    co = readfile(base, "present");
    if (co == NULL || co[0] != '1') {
        if (co != NULL) free(co);
        return smprintf("not present");
    }
    free(co);

    co = readfile(base, "charge_full");
    if (co == NULL) {
        co = readfile(base, "energy_full");
        if (co == NULL)
            return smprintf("");
    }
    sscanf(co, "%d", &descap);
    free(co);

    co = readfile(base, "charge_now");
    if (co == NULL) {
        co = readfile(base, "energy_now");
        if (co == NULL)
            return smprintf("");
    }
    sscanf(co, "%d", &remcap);
    free(co);

    co = readfile(base, "status");
    sscanf(co, "%s", stat);
    free(co);

    if (remcap < 0 || descap < 0)
        return smprintf("invalid");


    bat = ((float)remcap / (float)descap) * 100;

    if(strncmp(stat, "Discharging", 11) == 0) {
        if(bat < 20) {
            return smprintf("%s%.0f%%%s", red, bat, reset);

        } else if(bat > 80) {
            return smprintf("%s%.0f%%%s", green, bat, reset);
        } else {
            return smprintf("%s%.0f%%%s", yellow, bat, reset);
        }
    } else if(strncmp(stat, "Charging", 8) == 0) {
        return smprintf("%s%.0f%%%s", blue, bat, reset);
    } else {
        return smprintf("%s%.0f%%%s", blue, bat, reset);
    }
}
// END BATTERY

// TEMPERATURE
char *
get_temp(char *base, char *sensor)
{
    char *co;

    co = readfile(base, sensor);
    if (co == NULL)
        return smprintf("");
    return smprintf("%02.0f C", atof(co) / 1000);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	memset(buf, 0, sizeof(buf));
	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

int
main(void)
{
	char *status;
	char *temp;
	char *avgs;
	char *tmbln;
	char *netstats;
	char *battery;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(2)) {
        temp = get_temp("/sys/class/hwmon/hwmon0", "temp1_input");
		avgs = loadavg();
		tmbln = mktimes("%a, %d %b %H:%M %Y", tzberlin);
        netstats = get_netusage();
        battery = getbattery("/sys/class/power_supply/BAT0/");

        status = smprintf("T %s|L %s|N %s|B %s|%s",
                          temp, avgs, netstats, battery, tmbln);
		setstatus(status);
		free(avgs);
		free(temp);
		free(tmbln);
		free(status);
		free(netstats);
        free(battery);
	}

	XCloseDisplay(dpy);

	return 0;
}

