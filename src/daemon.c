/**
 * WSGI hot reloading
 * John Roman <john.roman@hearst.com>
 *
 * daemon.c
 * this program runs as a daemon within a docker container
 * to monitor project files, if they are updated this will
 * restart the WSGIDaemon procs so the changes are seen in
 * realtime. 
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_WATCHERS 524288 // max number of watchers linux allows

//TODO: Add logging since we are now running as a daemon

static int running = 0;
static int watchers[MAX_WATCHERS]; // array of watcher id's
static char *prog_name = NULL;
static char *root_dir = NULL;
static char *wsgi_file = NULL;
static char *pid_file_name = NULL;
static int pid_fd = -1;

static void displayInotifyEvent(struct inotify_event *ev)
{
    syslog(LOG_INFO, "wd =%2d; ", ev->wd);
    if (ev->cookie > 0)
        syslog(LOG_INFO, "cookie =%4d; ", ev->cookie);

    syslog(LOG_INFO, "mask = ");
    if (ev->mask & IN_ATTRIB)        syslog(LOG_INFO, "IN_ATTRIB ");
    if (ev->mask & IN_MODIFY)        syslog(LOG_INFO, "IN_MODIFY ");
    syslog(LOG_INFO, "\n");

    if (ev->len > 0)
        syslog(LOG_INFO, "name = %s\n", ev->name);
    
}

int add_watcher(int fd, const char *dir)
{
    int wd = inotify_add_watch(fd, dir, IN_MODIFY | IN_ATTRIB);
    if (wd == -1) {
        syslog(LOG_INFO, "error watching %s with wd: %d\n", dir, wd);
        
        return EXIT_FAILURE;
    }

    return wd;
}

int remove_watcher(int fd, int wd)
{
    int status = inotify_rm_watch(fd, wd);
    if (status == -1) {
        syslog(LOG_INFO, "error removing watcher %d\n", wd);
        
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int cleanup_watchers(int fd)
{
    int i = 0;
    for (;i < sizeof(watchers) / sizeof(int); i++) {
        if (remove_watcher(fd, watchers[i]) == -1)
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int create_watchers(int fd, const char *root_dir, char *wsgi_file)
{
    DIR *dirp;
    struct dirent *dn;
    int wd, iter;

    if ((dirp = opendir(root_dir)) == NULL) {
        syslog(LOG_ERR, "could not open directory %s\n", root_dir);
        
        return EXIT_FAILURE;
    }

    iter = 1;
    while ((dn = readdir(dirp)) != NULL) {
        if (dn->d_type & DT_DIR &&
                strcmp(dn->d_name, ".") != 0 && 
                strcmp(dn->d_name, "..") != 0 &&
                strcmp(dn->d_name, "static") != 0) {
            int path_len;
            char path[PATH_MAX]; // specified in limits.h

            path_len = snprintf(path, PATH_MAX, "%s/%s", 
                    root_dir, dn->d_name);

            if (path_len >= PATH_MAX) {
                syslog(LOG_ERR, "Path length is too long.\n");
                
                return EXIT_FAILURE;
            }

            wd = add_watcher(fd, path);
            if (wd == -1)
                return EXIT_FAILURE;

            syslog(LOG_INFO, "watching %s using wd %d\n", path, wd);
            
            watchers[iter++] = wd;

            create_watchers(fd, path, wsgi_file);
        }
    }

    closedir(dirp);

    return EXIT_SUCCESS;
}

int touch_wsgi(int fd, char *wsgi_file)
{
    FILE *fp;

    fp = fopen(wsgi_file, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "invalid file descriptor\n");
        
        return EXIT_FAILURE;
    }

    fclose(fp);
    syslog(LOG_INFO, "touched wsgi!\n");

    return EXIT_SUCCESS;
}

int monitor(int inotify_fd, const char *root_dir, char *wsgi_file)
{
    char inotify_buf[BUF_LEN];
    ssize_t num_read;
    struct inotify_event *event;
    char *wsgi_file_name = basename(wsgi_file);

    int wd = add_watcher(inotify_fd, root_dir);
    if (wd == -1)
        return EXIT_FAILURE;

    watchers[0] = wd;
    syslog(LOG_INFO, "watching %s using wd %d\n", root_dir, wd);
    

    // look for all nested dirs and create watchers for them
    create_watchers(inotify_fd, root_dir, wsgi_file);

    // signal handler affects this value
    running = 1;
    while (running == 1) {
        num_read = read(inotify_fd, inotify_buf, BUF_LEN);
        if (num_read == 0) {
            syslog(LOG_ERR, "read() from inotify returned 0\n");
            return EXIT_FAILURE;
        }

        if (num_read == -1) {
            syslog(LOG_INFO, "error reading inotify fd\n");
            return EXIT_FAILURE;
        }
        syslog(LOG_INFO, "read %ld bytes from inotify\n", (long)num_read);
        

        char *tmp = inotify_buf;
        while (tmp < inotify_buf + num_read) {
            event = (struct inotify_event *)tmp;
            if ((strcmp(event->name, wsgi_file_name) == 0)) {
                syslog(LOG_INFO, "skip wsgi file\n");
                tmp += sizeof(struct inotify_event) + event->len;
                continue;
            }
            displayInotifyEvent(event);
            if (touch_wsgi(inotify_fd, wsgi_file) == -1)
                return EXIT_FAILURE;

            tmp += sizeof(struct inotify_event) + event->len;
        }
    }

    return EXIT_SUCCESS;
}

// make prog run as a daemon process
static void daemonize()
{
    pid_t pid = 0;

    // fork off of parent proc
    pid = fork();

    // error occured
    if (pid < 0) {
        syslog(LOG_ERR, "could not fork process!\n");
        exit(EXIT_FAILURE);
    }

    // if we got a good pid, exit the parent proc
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // set SID for new child proc
    if (setsid() < 0) {
        syslog(LOG_ERR, "could not set sid on child process!\n");
        exit(EXIT_FAILURE);
    }
    
    // ignore signal sent from child to parent
    signal(SIGCHLD, SIG_IGN);

    // fork off for the second time
    pid = fork();

    // error occured
    if (pid < 0) {
        syslog(LOG_ERR, "could not fork process #2!\n");
        exit(EXIT_FAILURE);
    }
       
    // success let parent terminate
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // change file mode mask
    umask(0);

    if ((chdir("/")) < 0) {
        syslog(LOG_ERR, "could change to root dir!\n");
        exit(EXIT_FAILURE);
    }

    // close standard file descriptiors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);


    if (pid_file_name != NULL)
    {
        char str[256];
        pid_fd = open(pid_file_name, O_RDWR|O_CREAT, 0640);
        if (pid_fd < 0) {
            exit(EXIT_FAILURE);
        }
        if (lockf(pid_fd, F_TLOCK, 0) < 0) {
            exit(EXIT_FAILURE);
        }
        // get pid & save in str
        sprintf(str, "%d\n", getpid());
        // write to pid file
        ssize_t ret = write(pid_fd, str, strlen(str));
        if (ret < 0)
            exit(EXIT_FAILURE);
    }
}

// handle caught signal
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // TODO implement pid file close and such
        syslog(LOG_INFO, "shutting down...\n");
        running = 0;
        // reset signal handling
        signal(SIGINT, SIG_DFL);
    }
}

// TODO create help function for displaying usage
void print_usage(void)
{
    printf("\n Usage: %s [OPTIONS]\n\n", prog_name);
    printf("    Options:\n");
    printf("   -h --help                   Print this help\n");
    printf("   -w --watch_dir  filename   Test configuration file\n");
    printf("   -f --wsgi_file  filename   Test configuration file\n");
    printf("   -l --log_file    filename   Write logs to the file\n");
    printf("   -d --daemon                 Daemonize this application\n");
    printf("   -p --pid_file    filename   PID file used by daemonized app\n");
    printf("\n");
}


// Main Logic
int main(int argc, char **argv)
{
    // TODO implement actual option handling using getopt
    // for long options and such
    static struct option long_options[] = {
        {"watch_dir", required_argument, 0, 'w'},
        {"wsgi_file", required_argument, 0, 'f'},
        {"pid_file", required_argument, 0, 'p'},
        {"daemon", no_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {NULL, 0, 0, 0}
    };

    int opt_val, opt_idx = 0;
    int start_as_daemon = 0;

    prog_name = argv[0];

    while ((opt_val = getopt_long(argc, argv, "w:f:l:p:dh", long_options,
            &opt_idx)) != -1) {
        switch (opt_val) {
            case 'w':
                root_dir = strdup(optarg);
                break;
            case 'f':
                wsgi_file = strdup(optarg);
                break;
            case 'p':
                pid_file_name = strdup(optarg);
                break;
            case 'd':
                start_as_daemon = 1;
                break;
            case 'h':
                print_usage();
                exit(EXIT_SUCCESS);
            default:
                print_usage();
                exit(EXIT_FAILURE);
        }
    }

    if (start_as_daemon == 1) {
        daemonize();
    }


    openlog(argv[0], LOG_PID|LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "started: %s", prog_name);

    // catch SIGINT and handle it
    signal(SIGINT, signal_handler);

    int inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        syslog(LOG_ERR, "error inotify init, root: %s\n", root_dir);
        exit(EXIT_FAILURE);
    }

    // the main loop of the program, runs until signal caught
    if (!monitor(inotify_fd, root_dir, wsgi_file))
        exit(EXIT_FAILURE);

    syslog(LOG_INFO, "stopped: %s", prog_name);
    closelog();

    cleanup_watchers(inotify_fd);
    close(inotify_fd);

    if (root_dir != NULL) free(root_dir);
    if (wsgi_file != NULL) free(root_dir);
    if (pid_file_name != NULL) free(root_dir);

    exit(EXIT_SUCCESS);
}
