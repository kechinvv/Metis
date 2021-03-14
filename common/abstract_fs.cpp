#include "abstract_fs.h"

#include <algorithm>

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

struct md5sum {
  uint64_t a;
  uint64_t b;
};

struct exclude_path {
  std::string parent;
  std::string name;
};

static const struct exclude_path exclusion_list[] = {
  {"/", "lost+found"},
  {"/", ".nilfs"},
  {"/", ".mcfs_dummy"}
};

static inline bool is_excluded(const std::string &parent,
                               const std::string &name) {
  int N = sizeof(exclusion_list) / sizeof(struct exclude_path);
  for (int i = 0; i < N; ++i) {
    if (exclusion_list[i].parent == parent && exclusion_list[i].name == name) {
      return true;
    }
  }
  return false;
}

static inline bool is_this_or_parent(const char *name) {
  return (strncmp(name, ".", NAME_MAX) == 0) ||
         (strncmp(name, "..", NAME_MAX) == 0);
}

/**
 * hash_file_content: Feed the file content into MD5 calculator and update
 *   an existing MD5 context object.
 *
 * @param[in] fullpath: Full absolute path to the file being hashed
 * @param[in] md5ctx: Pointer to an initialized MD5_CTX object
 *
 * @return: 0 for success, +1 for MD5_Update failure,
 *          negative number for error status of open() or read()
 */
static int hash_file_content(const char *fullpath, MD5_CTX *md5ctx) {
  int fd = open(fullpath, O_RDONLY);
  char buffer[4096] = {0};
  ssize_t readsize;
  int ret = 0;
  if (fd < 0) {
    fprintf(stderr, "hash error: cannot open '%s' (%d)\n", fullpath, errno);
    ret = -errno;
    goto end;
  }

  while ((readsize = read(fd, buffer, 4096)) > 0) {
    ret = MD5_Update(md5ctx, buffer, readsize);
    memset(buffer, 0, sizeof(buffer));
    /* MD5_Update returns 0 for failure and 1 for success.
     * However, we want 0 for success and other values for error.
     */
    if (ret == 0) {
      /* This is special: If returned value is +1, then it indicates
       * MD5_Update error. Minus number for error in open() and read() */
      ret = 1;
      fprintf(stderr, "MD5_Update failed on file '%s'\n", fullpath);
      goto end;
    } else {
      ret = 0;
    }
  }
  if (readsize < 0) {
    fprintf(stderr, "hash error: read error on '%s' (%d)\n", fullpath, errno);
    ret = -errno;
  }

end:
  close(fd);
  return ret;
}

static int walk(const char *path, const char *abstract_path, absfs_t *fs,
                bool verbose, FILE *outf) {
  AbstractFile file;
  struct stat fileinfo = {0};
  std::vector<std::string> children;
  int ret = 0;
  // Avoid '.' or '..'
  if (is_this_or_parent(path)) {
    return 0;
  }

  file.fullpath = path;
  file.abstract_path = abstract_path;

  /* Stat the current file.
   * Use lstat() because we want to see symbol links as concrete files */
  ret = lstat(path, &fileinfo);
  if (ret != 0) {
    fprintf(stderr, "Walk error: cannot stat '%s' (%d)\n", path, errno);
    return -1;
  }
  memset(&file.attrs, 0, sizeof(file.attrs));
  /* Assemble attributes of our interest */
  file.attrs.mode = fileinfo.st_mode;
  file.attrs.size = fileinfo.st_size;
  file.attrs.nlink = fileinfo.st_nlink;
  file.attrs.uid = fileinfo.st_uid;
  file.attrs.gid = fileinfo.st_gid;

  if (verbose) {
    fprintf(outf, "%s, mode=", abstract_path);
    print_filemode(outf, file.attrs.mode);
    fprintf(outf, ", size=%zu", file.attrs.size);
    if (!S_ISREG(file.attrs.mode))
      fprintf(outf, " (Ignored), ");
    else
      fprintf(outf, ", ");
    fprintf(outf, "nlink=%ld, uid=%d, gid=%d\n", file.attrs.nlink,
            file.attrs.uid, file.attrs.gid);
  }

  /* Update the MD5 signature of the abstract file system state */
  file.FeedHasher(&fs->ctx);

  /* If the current file is a directory, read its entries and sort
   * Sorting makes sure that the order of files and directories
   * retrieved is deterministic.*/
  if (S_ISDIR(file.attrs.mode)) {
    DIR *dir = opendir(path);
    if (!dir) {
      fprintf(stderr, "Walk: unable to opendir '%s'. (%d)\n", path, errno);
      return -1;
    }
    struct dirent *child;
    while ((child = readdir(dir)) != NULL) {
      if (is_this_or_parent(child->d_name))
        continue;
      children.push_back(child->d_name);
    }
    closedir(dir);
    std::sort(children.begin(), children.end());
  }

  /* Walk childrens if there is any */
  for (std::string filename : children) {
    /* Ignored paths listed in exclusion_list */
    if (is_excluded(abstract_path, filename))
      continue;
    fs::path childpath = file.fullpath / filename;
    fs::path child_abstract_path = file.abstract_path / filename;
    ret = walk(childpath.c_str(), child_abstract_path.c_str(), fs, verbose,
               outf);
    if (ret < 0) {
      fprintf(stderr, "Error when walking '%s'.\n", childpath.c_str());
      return -1;
    }
  }
  return 0;
}

void AbstractFile::FeedHasher(MD5_CTX *ctx) {
  const char *abspath = abstract_path.c_str();
  size_t pathlen = strnlen(abspath, PATH_MAX);

  /* We only take file sizes of regular files into consideration,
   * because different file systems may have different behavior in
   * reporting special files' sizes (especially directories), which
   * is normal but will cause false discrepancy.
   */
  size_t fsize = attrs.size;
  if (!S_ISREG(attrs.mode)) {
    attrs.size = 0;
    attrs.nlink = 0;
  }

  MD5_Update(ctx, abspath, pathlen);
  MD5_Update(ctx, &attrs, sizeof(attrs));

  if (S_ISREG(attrs.mode))
    hash_file_content(fullpath.c_str(), ctx);

  /* Assign value back after use */
  attrs.size = fsize;
}

/**
 * init_abstract_fs: Initialize the abstract file system state
 *
 * @param[in]: Pointer to an absfs_t object.
 */
void init_abstract_fs(absfs_t *absfs) {
  MD5_Init(&absfs->ctx);
  memset(absfs->state, 0, sizeof(absfs->state));
}

/**
 * scan_abstract_fs: Walk the directory tree starting from the given
 *   basepath, and calculate a MD5 hash as the "abstract file system
 *   state".
 *
 * @param[in] absfs: The abstract file system object
 * @param[in] basepath: The path to start traversing
 *
 * @return: 0 for success, and other values for errors.
 */
int scan_abstract_fs(absfs_t *absfs, const char *basepath, bool verbose,
                     FILE *verbose_outf) {
  int ret = walk(basepath, "/", absfs, verbose, verbose_outf);
  MD5_Final(absfs->state, &absfs->ctx);
  return ret;
}

/**
 * print_abstract_fs_state: Print the whole 128-bit abstract file
 *   system state signature
 *
 * @param[in] out:   The FILE object that the caller want to output into
 * @param[in] state: The absfs_state_t value
 */
void print_abstract_fs_state(FILE *out, absfs_state_t state) {
  for (int i = 0; i < 16; ++i)
    fprintf(out, "%02x", state[i] & 0xff);
}

void print_filemode(FILE *out, mode_t mode) {
  fputc('<', out);

  /* file type */
  if (S_ISDIR(mode))
    fprintf(out, "dir ");
  if (S_ISCHR(mode))
    fprintf(out, "chrdev ");
  if (S_ISBLK(mode))
    fprintf(out, "blkdev ");
  if (S_ISREG(mode))
    fprintf(out, "file ");
  if (S_ISLNK(mode))
    fprintf(out, "symlink ");
  if (S_ISSOCK(mode))
    fprintf(out, "socket ");
  if (S_ISFIFO(mode))
    fprintf(out, "fifo ");

  /* permission */
  if (mode & S_ISUID)
    fprintf(out, "suid ");
  if (mode & S_ISGID)
    fprintf(out, "sgid ");
  if (mode & S_ISVTX)
    fprintf(out, "sticky ");
  fprintf(out, "%03o>", mode & 0777);
}

#ifdef ABSFS_TEST

int main(int argc, char **argv) {
  absfs_t absfs;
  char *basepath;
  int ret;

  if (argc > 1) {
    basepath = argv[1];
  } else {
    basepath = getenv("HOME");
  }

  init_abstract_fs(&absfs);

  printf("Iterating directory '%s'...\n", basepath);

  ret = scan_abstract_fs(&absfs, basepath, true, stdout);

  if (ret) {
    printf("Error occurred when iterating...\n");
  } else {
    printf("Iteration complete. Abstract FS signature = ");
    print_abstract_fs_state(stdout, absfs.state);
    printf("\n");
  }

  return ret;
}

#endif