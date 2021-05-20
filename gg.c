#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<fts.h>
#include<errno.h>
#include<regex.h>
#include<unistd.h>

#define VERSION "1.0.0"
#define MAX_SIZE (1024 * 1024)

/*    understand/
 * ignore some standard directories that are generally
 * not useful in search results
 */
static char* IGNORE_DIRS[] = {
  ".git", ".hg", ".svn", ".bzr",
  "target", "node_modules"
};

/*    understand/
 * helper functions for output
 */
int p(char *msg) { return printf("%s\n", msg); }
int err(char *fmt, char *msg) {
  if(msg) {
    char m[2048];
    sprintf(m, fmt, msg);
    perror(m);
  } else {
    perror(fmt);
  }
  return -1;
}
/* debugging helpers */
void d(int num) { printf("%d\n", num); }
void dd(int num, char* msg) { printf("%d %s\n", num, msg); }

/*    understand/
 * tells the program what to do based on user input
 */
struct config {
  int help;
  int ignorecase;
  char expr[1024];
};

/*    way/
 * if the user has not provided enough arguments,
 * show him the help. Otherwise take concaternate
 * all the arguments as a single regular expression
 */
struct config getConfig(int argc, char* argv[]) {
  struct config r;
  r.help = argc < 2;

  char* expr = r.expr;
  int i = 1;
  while(i < argc) {
    expr = stpcpy(expr, argv[i]);
    i++;
    if(i == argc) break;
    *expr = ' ';
    expr++;
  }

  r.ignorecase = 1;
  for(expr = r.expr;*expr;expr++) {
    if(*expr >= 'A' && *expr <= 'Z') r.ignorecase = 0;
  }

  return r;
}

/*    way/
 * return the number of processors on this machine
 */
unsigned int pcthread_get_num_procs()
{
    return (unsigned int)sysconf(_SC_NPROCESSORS_ONLN);
}


int showHelp() {
  p("gg: Quick Grep");
  p(VERSION);
  p("$> gg <regular expression>");
  p("      - finds all matches in code/text files");
  p("      - uses sMartCase (ignores case by default)");

  return 0;
}

/****** structure to save list of files *******/
#define FL_BLOCK_SZ   1024*5
struct fl_block {
  int num;
  char names[FL_BLOCK_SZ];
  int ndx;
  struct fl_block* nxt;
};
struct fl_block* fl_block_new() {
  struct fl_block* fl_block = malloc(sizeof(struct fl_block));
  fl_block->num = 0;
  fl_block->ndx = 0;
  fl_block->nxt = 0;
  return fl_block;
}
void fl_block_add(char* name, size_t len, struct fl_block* fl_block) {
  strcpy(fl_block->names + fl_block->ndx, name);
  fl_block->ndx += len;
  fl_block->num++;
}
struct fl_ {
  struct fl_block* first;
  struct fl_block* last;
};
struct fl_* fl_new() {
  struct fl_* fl_ = malloc(sizeof(struct fl_));
  fl_->first = fl_->last = fl_block_new();
  return fl_;
}
/*    way/
 * If there is not enough space to add the
 * new name to the last block, create a new
 * last block and add it there.
 */
void fl_add(char* name, struct fl_* fl_) {
  if(!name) return;
  size_t len = strlen(name) + 1;
  size_t left = FL_BLOCK_SZ - fl_->last->ndx;
  if(len >= left) {
    fl_->last->nxt = fl_block_new();
    fl_->last = fl_->last->nxt;
  }
  return fl_block_add(name, len, fl_->last);
}
int fl_walk(struct fl_* fl_, int (*cb)(char* name, void* ctx), void* ctx) {
  struct fl_block* fl_block = fl_->first;
  while(fl_block) {
    int ndx = 0;
    for(int i = 0;i < fl_block->num;i++) {
      char *name = fl_block->names + ndx;
      int ret = cb(name, ctx);
      if(ret == -1) return ret;
      ndx += strlen(name) + 1;
    }
    fl_block = fl_block->nxt;
  }
  return 0;
}
struct fl_threads {
  int fl_num;
  struct fl_** fls;
  int num_added;
};
/*    way/
 * create a set of file-lists based one for each
 * thread
 */
struct fl_threads* fl_threads_new() {
  struct fl_threads* fl_threads = malloc(sizeof(struct fl_threads));
  fl_threads->num_added = 0;
  fl_threads->fl_num = pcthread_get_num_procs() + 1;
  fl_threads->fls = malloc(fl_threads->fl_num * sizeof(struct fl_*));
  for(int i = 0;i < fl_threads->fl_num;i++) fl_threads->fls[i] = fl_new();
  return fl_threads;
}
/*    understand/
 * add each new name to a different thread file-list
 */
void fl_threads_add(char* name, struct fl_threads* fl_threads) {
  struct fl_* fl_ = fl_threads->fls[fl_threads->num_added % fl_threads->fl_num];
  fl_add(name, fl_);
  fl_threads->num_added++;
}

/****** walk the directories finding files ******/
int fts_err(FTSENT *node) {
  if(node->fts_info == FTS_DNR ||
      node->fts_info == FTS_ERR ||
      node->fts_info == FTS_NS) return node->fts_errno;
  return 0;
}

int skip(FTSENT *node) {
  if(node->fts_info == FTS_F) {
    return 0;
    return node->fts_statp->st_size > MAX_SIZE;
  }
  int num = sizeof(IGNORE_DIRS)/sizeof(IGNORE_DIRS[0]);
  for(int i = 0;i < num;i++) {
    if(strcmp(IGNORE_DIRS[i], node->fts_name) == 0) return 1;
  }
  return 0;
}

int findFiles(struct fl_threads* fl_threads) {
  char * curr[] = {".", 0};
  FTS *tree = fts_open(curr, FTS_LOGICAL, 0);
  if(!tree) return err("Failed opening directory", NULL);

  FTSENT *node;
  errno = 0;
  while((node = fts_read(tree))) {
    errno = fts_err(node);
    if(errno) {
      err("Error walking %s", node->fts_path);
      errno = 0;
    } else if(skip(node)) {
      if(node->fts_info == FTS_D) fts_set(tree, node, FTS_SKIP);
    } else {
      if(node->fts_info == FTS_F) {
        fl_threads_add(node->fts_path, fl_threads);
      }
    }
  }
  fts_close(tree);
  if(errno) return err("Failed walking directory", NULL);
  return 0;
}

static char buf[MAX_SIZE];
int grep(regex_t* rx, char *path, char *currpath) {
  regmatch_t m[1];

  FILE *f = fopen(currpath, "r");
  if(!f) return err("Failed opening %s", path);
  errno = 0;
  int sz = fread(buf, 1, MAX_SIZE, f);
  if(errno) {
    err("Failed reading %s", path);
    fclose(f);
    return -1;
  }
  fclose(f);
  buf[sz] = 0;

  int chk = sz > 256 ? 256 : sz;
  for(int i = 0;i < sz;i++) if(!buf[i]) return 0;

  int s = 0;
  int ls = 0;
  int lnum = 1;
  while(regexec(rx, buf+s, 1, m, 0) == 0) {
    for(int i = 0;i < m->rm_so;i++) {
      if(buf[s+i] == '\n') {
        lnum++;
        ls = s+i+1;
      }
    }
    // while(buf[ls] == ' ' || buf[ls] == '\t') ls++;
    s += m->rm_eo;
    for(;s < sz;s++) if(buf[s] == '\n') break;

    if(s - ls > 128) {
      char op[128];
      memcpy(op, buf+ls, 127);
      op[124] = op[125] = op[126] = '.';
      op[127] = 0;
      printf("%s:%d:%s\n", path, lnum, op);
    } else {
      if(s < sz) buf[s] = 0;
      printf("%s:%d:%s\n", path, lnum, buf+ls);
      if(s < sz) buf[s] = '\n';
    }
  }

  return 0;
}

int search(struct config *config) {
  regex_t rx;
  int flags = REG_NEWLINE;
  if(config->ignorecase) flags |= REG_ICASE;
  int e = regcomp(&rx, config->expr, flags);
  if(e) return err("Bad regular expression '%s'", config->expr);

  char * curr[] = {".", 0};
  FTS *tree = fts_open(curr, FTS_LOGICAL, 0);
  if(!tree) return err("Failed opening directory", NULL);

  FTSENT *node;
  errno = 0;
  while((node = fts_read(tree))) {
    errno = fts_err(node);
    if(errno) {
      err("Error walking %s", node->fts_path);
      errno = 0;
    } else if(skip(node)) {
      if(node->fts_info == FTS_D) fts_set(tree, node, FTS_SKIP);
    } else {
      if(node->fts_info == FTS_F) {
        grep(&rx, node->fts_path + 2, node->fts_accpath);
      }
    }
  }
  fts_close(tree);
  regfree(&rx);
  if(errno) return err("Failed walking directory", NULL);

  return 0;
}


int print_names(char* name, void* ctx) {
  printf("%s\n", name);
  return 0;
}


/*    way/
 * show help if needed or do the search
 */
int main(int argc, char* argv[]) {
  struct fl_threads* fl_threads = fl_threads_new();
  findFiles(fl_threads);

  for(int i = 0;i < fl_threads->fl_num;i++) {
    fl_walk(fl_threads->fls[i], print_names, 0);
  }

  /*
  struct config config = getConfig(argc, argv);
  if(config.help) return showHelp();
  else return search(&config);*/
}

