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
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_WATCHERS 524288 // max number of watchers linux allows

//TODO: Add logging since we are now running as a daemon

// array of watcher id's
int watchers[MAX_WATCHERS];

static void displayInotifyEvent(struct inotify_event *i) {
	printf("    wd =%2d; ", i->wd);
	if (i->cookie > 0)
		printf("cookie =%4d; ", i->cookie);

	printf("mask = ");
	if (i->mask & IN_ACCESS)        printf("IN_ACCESS ");
	if (i->mask & IN_ATTRIB)        printf("IN_ATTRIB ");
	if (i->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE ");
	if (i->mask & IN_CLOSE_WRITE)   printf("IN_CLOSE_WRITE ");
	if (i->mask & IN_CREATE)        printf("IN_CREATE ");
	if (i->mask & IN_DELETE)        printf("IN_DELETE ");
	if (i->mask & IN_DELETE_SELF)   printf("IN_DELETE_SELF ");
	if (i->mask & IN_IGNORED)       printf("IN_IGNORED ");
	if (i->mask & IN_ISDIR)         printf("IN_ISDIR ");
	if (i->mask & IN_MODIFY)        printf("IN_MODIFY ");
	if (i->mask & IN_MOVE_SELF)     printf("IN_MOVE_SELF ");
	if (i->mask & IN_MOVED_FROM)    printf("IN_MOVED_FROM ");
	if (i->mask & IN_MOVED_TO)      printf("IN_MOVED_TO ");
	if (i->mask & IN_OPEN)          printf("IN_OPEN ");
	if (i->mask & IN_Q_OVERFLOW)    printf("IN_Q_OVERFLOW ");
	if (i->mask & IN_UNMOUNT)       printf("IN_UNMOUNT ");
	printf("\n");

	if (i->len > 0)
		printf("        name = %s\n", i->name);
}

int add_watcher(int fd, const char *dir) {
    int wd = inotify_add_watch(fd, dir, IN_MODIFY | IN_ATTRIB);
    if (wd == -1) {
        printf("error watching %s with wd: %d\n", dir, wd);
        return -1;
    }

    return wd;
}

int remove_watcher(int fd, int wd) {
    int status = inotify_rm_watch(fd, wd);
    if (status == -1) {
        printf("error removing watcher %d\n", wd);
        return -1;
    }

    return 0;
}

int cleanup_watchers(int fd) {
    int i = 0;
    for (;i < sizeof(watchers) / sizeof(int); i++) {
        if (remove_watcher(fd, watchers[i]) == -1) {
            return -1;
        }
    }

    return 0;
}

int touch_wsgi(int fd, char *wsgi_file) {
    FILE *fp;

    fp = fopen(wsgi_file, "w");
    if (fp == NULL) {
        printf("invalid file descriptor\n");
        return -1;
    }

    fclose(fp);
    printf("touched wsgi!\n");

    return 0;
}

int create_watchers(int fd, const char *root_dir, char *wsgi_file) {
    DIR *dirp;
    struct dirent *dn;
    int wd, iter;

    if ((dirp = opendir(root_dir)) == NULL) {
        printf("could not open directory %s\n", root_dir);
        return -1;
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
                printf("Path length is too long.\n");
                return -1;
            }

            wd = add_watcher(fd, path);
            if (wd == -1) {
                return -1;
            }
            printf("watching %s using wd %d\n", path, wd);
            watchers[iter++] = wd;

            create_watchers(fd, path, wsgi_file);
        }
    }

    closedir(dirp);
    
    return 0;
}


int monitor(int inotify_fd, const char *root_dir, char *wsgi_file) {
    char inotify_buf[BUF_LEN];
    ssize_t num_read;
    struct inotify_event *event;
    char *wsgi_file_name = basename(wsgi_file);

    int wd = add_watcher(inotify_fd, root_dir);
    if (wd == -1) {
        return -1;
    }
    watchers[0] = wd;
    printf("watching %s using wd %d\n", root_dir, wd);

    // look for all nested dirs and create watchers for them
    create_watchers(inotify_fd, root_dir, wsgi_file);

    while (1) {
        num_read = read(inotify_fd, inotify_buf, BUF_LEN);
        if (num_read == 0) {
            printf("read() from inotify returned 0\n");
            return -1;
        }

        if (num_read == -1) {
            printf("error reading inotify fd\n");
            return -1;
        }
        printf("read %ld bytes from inotify\n", (long)num_read);


        char *tmp = inotify_buf;
        while (tmp < inotify_buf + num_read) {
            event = (struct inotify_event *)tmp;
            if ((strcmp(event->name, wsgi_file_name) == 0)) {
                printf("skip wsgi file\n");
                tmp += sizeof(struct inotify_event) + event->len;
                continue;
            }
            displayInotifyEvent(event);
            if (touch_wsgi(inotify_fd, wsgi_file) == -1) return -1;
            tmp += sizeof(struct inotify_event) + event->len;
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    // check that directory and wsgi location were entered
    if (argc < 3) {
        printf("enter watch root dir and wsgi location!\n");
        exit(EXIT_FAILURE);
    }

    const char *root_dir = argv[1];
    char *wsgi_file = argv[2];
    pid_t pid, sid;

    pid = fork();
    if (pid < 0) {
        printf("could not fork process!\n");
        exit(EXIT_FAILURE);
    }

    // if we got a good pid, exit the parent proc
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // change file mode mask
    umask(0);
    // set SID for new child proc
    sid = setsid();
    if (sid < 0) {
        printf("could not set sid on child process!\n");
        exit(EXIT_FAILURE);
    }
    printf("child proc sid: %d\n", sid);

    if ((chdir("/")) < 0) {
        printf("could change to root dir!\n");
        exit(EXIT_FAILURE);
    }

    // close standard file descriptiors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int inotify_fd = inotify_init(); if (inotify_fd == -1) {
        printf("error inotify init, root: %s\n", root_dir);
        exit(EXIT_FAILURE);
    }

    if (!monitor(inotify_fd, root_dir, wsgi_file)) {
        exit(EXIT_FAILURE);
    }

    cleanup_watchers(inotify_fd);
    close(inotify_fd);

    exit(EXIT_SUCCESS);
}
