/*
 * Copyright (c) 2020-2024 Yifei Liu
 * Copyright (c) 2020-2024 Wei Su
 * Copyright (c) 2020-2024 Erez Zadok
 * Copyright (c) 2020-2024 Stony Brook University
 * Copyright (c) 2020-2024 The Research Foundation of SUNY
 *
 * You can redistribute it and/or modify it under the terms of the Apache
 * License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0).
 */

#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 2

// If defined, using dbus to unexport NFS-Ganesha
#define NFS_GANESHA_UNEXPORT_ENABLED

#include "fileutil.h"

//static bool fs_frozen[N_FS] = {0};

static char *receive_output(FILE *cmdfp, size_t *length)
{
    const size_t block = 4096;
    char *buffer = malloc(block);
    size_t readsz = 0, bufsz = block;
    assert(buffer);
    while (!feof(cmdfp)) {
        if (readsz + block > bufsz) {
            char *newbuf = realloc(buffer, bufsz + block);
            if (!newbuf) {
                fprintf(stderr, "%s encountes out-of-memory issue, command "
                        "output is truncated at %zu bytes.\n", __func__,
                        readsz);
                break;
            }
            bufsz += block;
            buffer = newbuf;
        }
        char *ptr = buffer + readsz;
        readsz += fread(ptr, 1, block, cmdfp);
    }
    *length = readsz;
    return buffer;
}

bool do_fsck()
{
    char cmdbuf[ARG_MAX];
    bool isgood = true;
    for (int i = 0; i < get_n_fs(); ++i) {
        snprintf(cmdbuf, ARG_MAX, "fsck -N -t %s %s 2>&1", get_fslist()[i],
                 get_devlist()[i]);
        FILE *cmdfp = popen(cmdbuf, "r");
        size_t outlen = 0;
        char *output = receive_output(cmdfp, &outlen);
        int ret = pclose(cmdfp);
        if (ret != 0) {
            fprintf(stderr, "fsck %s failed and returned %d, %s may have been "
                    "corrupted.\n", get_devlist()[i], ret, get_fslist()[i]);
            fprintf(stderr, "Here's the output: \n");
            fwrite(output, 1, outlen, stderr);
            fprintf(stderr, "\n");
            isgood = false;
        }
        free(output);
    }
    return isgood;
}

void mountall()
{
    int failpos, err;
    char cmdbuf[PATH_MAX];
    for (int i = 0; i < get_n_fs(); ++i) {
        int ret = -1;
        /* Skip VeriFS and NFS/Ganesha with VeriFS */
        if (is_verifs(get_fslist()[i])) {
            continue;
        }
        /* mount(source, target, fstype, mountflags, option_str) */
        else if(is_nova(get_fslist()[i])) {
            snprintf(cmdbuf, PATH_MAX, "mount -t NOVA -o noatime %s %s", 
                get_devlist()[i], get_basepaths()[i]);
            ret = execute_cmd_status(cmdbuf);                       
        }
        else if (is_nfs_ganesha_ext4(get_fslist()[i])) {
            /* Mount NFS-Ganesha server export path
             * Mount first, otherwise cannot export this path 
             */
            ret = mount(get_devlist()[i], NFS_GANESHA_EXPORT_PATH, "ext4", MS_NOATIME, "");
            if (ret != 0) {
                failpos = i;
                err = errno;
                fprintf(stderr, "Could not mount file system %s at %s (%s)\n",
                        get_fslist()[i], NFS_GANESHA_EXPORT_PATH, errnoname(err));
                goto err;
            }
            /* Restart NFS-Ganesha service to export the server path */
            ret = start_nfs_ganesha_server(i);
            if (ret != 0) {
                failpos = i;
                err = errno;
                fprintf(stderr, "Could not start NFS-Ganesha server (%s)\n",
                        errnoname(err));
                goto err;
            }
            /* Mount NFS-Ganesha client after starting Ganesha server 
             * and exporting the server path
             */
            snprintf(cmdbuf, PATH_MAX, "mount.nfs4 -o vers=4 %s:%s %s", 
                NFS_GANESHA_LOCALHOST, NFS_GANESHA_EXPORT_PATH, get_basepaths()[i]);
            ret = execute_cmd_status(cmdbuf);
        }
        else if (is_nfs_ext4(get_fslist()[i])) {
            /* Mount NFS server export path */
            ret = mount(get_devlist()[i], NFS_EXPORT_PATH, "ext4", MS_NOATIME, "");
            if (ret != 0) {
                failpos = i;
                err = errno;
                fprintf(stderr, "Could not mount file system %s at %s (%s)\n",
                        get_fslist()[i], NFS_EXPORT_PATH, errnoname(err));
                goto err;
            }
            /* (Re)-export the NFS server path */
            ret = export_nfs_server(i);
            if (ret != 0) {
                failpos = i;
                err = errno;
                fprintf(stderr, "Could not start NFS server (%s)\n",
                        errnoname(err));
                goto err;
            }
            /* Mount NFS client after mounting the server export path */
            snprintf(cmdbuf, PATH_MAX, "mount -t nfs -o rw,nolock,vers=4,proto=tcp %s:%s %s", 
                NFS_LOCALHOST, NFS_EXPORT_PATH, get_basepaths()[i]);
            ret = execute_cmd_status(cmdbuf);
        }
        else {
            ret = mount(get_devlist()[i], get_basepaths()[i], get_fslist()[i], MS_NOATIME, "");
        }        
        if (ret != 0) {
            failpos = i;
            err = errno;
            goto err;
        }
    }
    return;
err:
    /* undo mounts */
    for (int i = 0; i < failpos; ++i) {
        if (is_verifs(get_fslist()[i]))
            continue;
        umount2(get_basepaths()[i], MNT_FORCE);
    }
    fprintf(stderr, "Could not mount file system %s in %s at %s (%s)\n",
            get_fslist()[failpos], get_devlist()[failpos], get_basepaths()[failpos],
            errnoname(err));
    exit(1);
}

static void save_lsof()
{
    int ret;
    static int report_count = 0;
    char progname[NAME_MAX] = {0};
    char logname[NAME_MAX] = {0};
    char cmd[PATH_MAX] = {0};

    get_progname(progname);
    add_ts_to_logname(logname, NAME_MAX, "lsof", progname, "");
    ret = snprintf(cmd, PATH_MAX, "lsof > %s-%d.txt", logname, report_count++);
    assert(ret >= 0);
    ret = system(cmd);
}

// Return has_failure (true: failure, false: success)
static bool unmount_with_retry(char *fsname, char *basepath) {
    int ret = -1;
    int retry_limit = 19;
    int num_retries = 0;
    bool has_failure = false;
    while (retry_limit > 0) {
        ret = umount2(basepath, 0);
        if (ret == 0) {
            break; // Success, exit the retry loop
        }        

        /* If unmounting failed due to device being busy, again up to
        * retry_limit times with 100 * 2^n ms (n = num_retries) */
        if (errno == EBUSY) {
            // 100 * (1 <<  0) = 100ms
            // 100 * (1 << 18) = 100 * 262144 = 26.2144s
            useconds_t waitms = 100 * (1 << num_retries); // Exponential backoff starting at 100ms
            fprintf(stderr, "File system %s mounted on %s is busy. Retry %d times,"
                    "unmounting after %dms.\n", fsname, basepath, num_retries + 1,
                    waitms);
            usleep(1000 * waitms);
            num_retries++;
            retry_limit--;
            save_lsof();
        } 
        else {
            // Handle non-EBUSY errors immediately without retrying
            fprintf(stderr, "Could not unmount file system %s at %s (%s)\n",
                    fsname, basepath, errnoname(errno));
            has_failure = true;
            break;
        }
        
        if (retry_limit == 0) {
            fprintf(stderr, "Failed to unmount file system %s at %s after retries.\n",
                    fsname, basepath);
            has_failure = true;
        }
    }
    return has_failure;
}

void unmount_all(bool strict)
{
    bool has_failure = false;
    int ret;
#ifndef NO_FS_STAT
    record_fs_stat();
#endif
    for (int i = 0; i < get_n_fs(); ++i) {
        // Change retry limit from 20 to 19 to avoid excessive delay
        char cmdbuf[PATH_MAX];
        /* Skip VeriFS and NFS/Ganesha with VeriFS */
        if (is_verifs(get_fslist()[i])) {
            continue;
        }
        else if (is_nfs_ganesha_ext4(get_fslist()[i])) {
            /* Unmount NFS-Ganesha client */
            ret = umount2(get_basepaths()[i], 0);
            if (ret != 0) {
                fprintf(stderr, "Client path: could not unmount file system %s at %s (%s)\n",
                        get_fslist()[i], get_basepaths()[i], errnoname(errno));
                has_failure = true;
            }
#ifdef NFS_GANESHA_UNEXPORT_ENABLED
            /* Unexport the Ganesha server export path */
            snprintf(cmdbuf, PATH_MAX, "dbus-send --system --type=method_call --print-reply --dest=org.ganesha.nfsd /org/ganesha/nfsd/ExportMgr org.ganesha.nfsd.exportmgr.RemoveExport uint16:%u", NFS_GANESHA_EXPORT_ID);
            ret = execute_cmd_status(cmdbuf);
            if (ret != 0) {
                fprintf(stderr, "D-bus server unexport: could not unexport file system %s at %s (%s)\n",
                        get_fslist()[i], get_basepaths()[i], errnoname(errno));
                has_failure = true;
            }
#else
            /* Stop NFS-Ganesha service instead of unexporting Ganesha server export path */
            snprintf(cmdbuf, PATH_MAX, "systemctl stop nfs-ganesha");
            ret = execute_cmd_status(cmdbuf);
            if (ret != 0) {
                fprintf(stderr, "Server stop: could not stop NFS-Ganesha service (%s)\n",
                        errnoname(errno));
                has_failure = true;
            }
#endif
            /* Unmount NFS-Ganesha server export path */
            ret = umount2(NFS_GANESHA_EXPORT_PATH, 0);
            if (ret != 0) {
                fprintf(stderr, "Server export: could not unmount file system %s at %s (%s)\n",
                        get_fslist()[i], NFS_GANESHA_EXPORT_PATH, errnoname(errno));
                has_failure = true;
            }
        }
        else if (is_nfs_ext4(get_fslist()[i])) {
            /* Unmount NFS client */
            ret = umount2(get_basepaths()[i], 0);
            if (ret != 0) {
                fprintf(stderr, "Client path: could not unmount file system %s at %s (%s)\n",
                        get_fslist()[i], get_basepaths()[i], errnoname(errno));
                has_failure = true;
            }
            /* Unexport NFS server */
            snprintf(cmdbuf, PATH_MAX, "exportfs -u %s:%s", NFS_LOCALHOST, NFS_EXPORT_PATH);
            ret = execute_cmd_status(cmdbuf);
            if (ret != 0) {
                fprintf(stderr, "Server unexport: could not unexport file system %s at %s (%s)\n",
                        get_fslist()[i], NFS_EXPORT_PATH, errnoname(errno));
                has_failure = true;
            }
            /* Unmount NFS server export path 
             * Handle EBUSY while unmounting NFS server export path 
             */
            if (unmount_with_retry(get_fslist()[i], NFS_EXPORT_PATH)) {
                fprintf(stderr, "Server export: could not unmount file system %s at %s (%s)\n",
                        get_fslist()[i], NFS_EXPORT_PATH, errnoname(errno));
                has_failure = true;
            }
        }
        /* Unmount the other file systems without using NFS */
        else {
            /* We have to unfreeze the frozen file system before unmounting it.
            * Otherwise the system will hang! */
            /*
            if (fs_frozen[i]) {
                fsthaw(get_fslist()[i], get_devlist()[i], get_basepaths()[i]);
            }
            */
            has_failure = unmount_with_retry(get_fslist()[i], get_basepaths()[i]) || has_failure;
        }
    }
    if (has_failure && strict)
        exit(1);
}

// static const int warning_limits = N_FS;
static int warnings_issued = 0;

static void set_fs_frozen_flag(const char *mountpoint, bool value)
{
    for (int i = 0; i < get_n_fs(); ++i) {
        if (strncmp(get_basepaths()[i], mountpoint, PATH_MAX) == 0) {
            fs_frozen[i] = value;
            break;
        }
    }
}

static int freeze_or_thaw(const char *caller, const char *fstype,
    const char *devpath, const char *mp, unsigned long op)
{
    if (op != FIFREEZE && op != FITHAW)
        return -1;

    char *opname;
    int mpfd = open(mp, O_RDONLY | __O_DIRECTORY);
    if (mpfd < 0) {
        fprintf(stderr, "%s: Cannot open %s (%s)\n", caller, mp,
                errnoname(errno));
        return -1;
    }

    if (op == FIFREEZE)
        opname = "FIFREEZE";
    else if (op == FITHAW)
        opname = "FITHAW";

    int ret = ioctl(mpfd, op, 0);
    int err = errno;
    close(mpfd);
    if (ret == 0) {
        /* Mark the corresponding file system as being frozen */
        set_fs_frozen_flag(mp, (op == FIFREEZE));
        return 0;
    }
    /* fall back to remounting the file system in read-only mode */
    if (warnings_issued < get_n_fs()) {
        fprintf(stderr, "%s: ioctl %s cannot be used on %s (%s). "
                "Falling back to remounting in r/o mode.\n", caller, opname,
                mp, errnoname(err));
        warnings_issued++;
    }

    int remnt_flag = MS_REMOUNT | MS_NOATIME;
    char *options = "";
    if (op == FIFREEZE)
        remnt_flag |= MS_RDONLY;
    else
        options = "rw";

    ret = mount(devpath, mp, fstype, remnt_flag, options);
    if (ret < 0) {
        fprintf(stderr, "%s: remounting failed on %s (%s)\n", caller, mp,
                errnoname(errno));
    }
    return (ret == 0) ? 0 : -1;

}

int fsfreeze(const char *fstype, const char *devpath, const char *mountpoint)
{
    return freeze_or_thaw(__func__, fstype, devpath, mountpoint, FIFREEZE);
}

int fsthaw(const char *fstype, const char *devpath, const char *mountpoint)
{
    return freeze_or_thaw(__func__, fstype, devpath, mountpoint, FITHAW);
}

int unfreeze_all()
{
    for (int i = 0; i < get_n_fs(); ++i) {
        if (fs_frozen[i]) {
            fprintf(stderr, "unfreezing %s at %s\n", get_fslist()[i], get_basepaths()[i]);
            fsthaw(get_fslist()[i], get_devlist()[i], get_basepaths()[i]);
        }
    }
}
