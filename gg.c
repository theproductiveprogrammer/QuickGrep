#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<fts.h>
#include<errno.h>
#include<regex.h>
#include<unistd.h>
#include<pthread.h>

#define VERSION "1.4.1"

#define BUF_SIZE (2 * 1024 * 1024)
#define MAX_FILE_SIZE BUF_SIZE

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
  int inVert;
  char *cwd;
  char expr[1024];
};

/*    way/
 * if the user has not provided enough arguments,
 * show him the help. Otherwise set the other
 * parameters (-v[erbose] and -c[hange dir]),
 * and concaternate all the rest of the arguments
 * as a single regular expression.
 */
struct config getConfig(int argc, char* argv[]) {
  struct config r;
  r.help = argc < 2;

  r.inVert = 0;
  r.cwd = ".";
  int a = 1;
  while(a < argc) {
    if(!strcmp(argv[a], "-h")) {
      r.help = 1;
      a++;
      continue;
    }
    if(!strcmp(argv[a], "-v")) {
      r.inVert = 1;
      a++;
      continue;
    }
    if(!strcmp(argv[a], "-c")) {
      a++;
      if(a < argc) {
        r.cwd = argv[a];
        a++;
        continue;
      }
    }
    break;
  }

  char* expr = r.expr;
  expr[0] = 0;
  while(a < argc) {
    expr = stpcpy(expr, argv[a]);
    a++;
    if(a == argc) break;
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

/*    way/
 * return a compiled regular expression using the ignore-case
 * and new-line flags
 */
int rxC(struct config* config, regex_t* rx) {
  int flags = REG_NEWLINE | REG_EXTENDED;
  if(config->ignorecase) flags |= REG_ICASE;
  return regcomp(rx, config->expr, flags);
}

int showHelp() {
  p("gg: Quick Grep");
  p(VERSION);
  p("$> gg [-h | -v | -c /path/...] <regular expression>");
  p("      * finds all matches in code/text files");
  p("      * uses sMartCase (ignores case by default)");
  p("");
  p("      -h: show help");
  p("      -v: verbose");
  p("      -c: search in /path/...");
  p("");

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
struct fl_ctx {
  int inVert;
  regex_t rx;
  char buf[BUF_SIZE];
};
struct fl_ {
  void* (*fn)(char* name, void* ctx);
  struct fl_ctx ctx;

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
void* fl_walk(struct fl_* fl_, void* (*cb)(char* name, void* ctx), void* ctx) {
  struct fl_block* fl_block = fl_->first;
  while(fl_block) {
    int ndx = 0;
    for(int i = 0;i < fl_block->num;i++) {
      char *name = fl_block->names + ndx;
      void* ret = cb(name, ctx);
      if(ret) return ret;
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
/*    way/
 * run the given function against all the entries in the file-list
 */
void* fl_thread_run(void* val) {
  struct fl_* fl_ = val;
  return fl_walk(fl_, fl_->fn, &fl_->ctx);
}

/*    way/
 * create each new thread, passing it the context needed to run,
 * and then wait for them all to finish - returning any errors
 * found
 */
int fl_threads_run(struct fl_threads* fl_threads,
    void* (*cb)(char* name, void* ctx),
    struct config* config) {

  pthread_t threads[fl_threads->fl_num];
  for(int i = 0;i < fl_threads->fl_num;i++) {
    struct fl_* fl_ = fl_threads->fls[i];
    fl_->fn = cb;
    fl_->ctx.inVert = config->inVert;
    rxC(config, &fl_->ctx.rx);
    pthread_create(&threads[i], 0, fl_thread_run, fl_);
  }
  int ret = 0;
  for(int i = 0;i < fl_threads->fl_num;i++) {
    ret |= pthread_join(threads[i], 0);
  }
  return ret;
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
    return node->fts_statp->st_size > MAX_FILE_SIZE;
  }
  int num = sizeof(IGNORE_DIRS)/sizeof(IGNORE_DIRS[0]);
  for(int i = 0;i < num;i++) {
    if(strcmp(IGNORE_DIRS[i], node->fts_name) == 0) return 1;
  }
  return 0;
}

int findFiles(char *cwd, struct fl_threads* fl_threads) {
  char * curr[] = {cwd, 0};
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

void showLine(char *path, char *buf, int sz, int ls, int s, int lnum) {
  if(path[0] == '.' && path[1] == '/') path += 2;
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

int grep(int inVert, int sz, char* buf, regex_t* rx, char* path) {
  regmatch_t m[1];

  int s = 0;
  int ls = 0;
  int lnum = 1;
  while(regexec(rx, buf+s, 1, m, 0) == 0) {
    for(int i = 0;i < m->rm_so;i++) {
      if(buf[s+i] == '\n') {
        if(inVert) showLine(path, buf, sz, ls, s+i, lnum);
        lnum++;
        ls = s+i+1;
      }
    }
    // while(buf[ls] == ' ' || buf[ls] == '\t') ls++;
    s += m->rm_eo;
    for(;s < sz;s++) if(buf[s] == '\n') break;

    if(!inVert) showLine(path, buf, sz, ls, s, lnum);
    s++; lnum++; ls = s;
    if(s >= sz) break;
  }

  if(inVert && s < sz) {
    for(int i = 0;(s+i) < sz;i++) {
      if(buf[s+i] == '\n') {
        showLine(path, buf, sz, ls, s+i, lnum);
        lnum++;
        ls = s+i+1;
      }
    }
  }

  return 0;
}

/*    way/
 * read in the entire file int a buffer
 * NB: we expect files to be smaller than the buffer
 * hence the MAX_FILE_SIZE #define above
 */
int readFile(char* path, char* buf) {
  FILE *f = fopen(path, "r");
  if(!f) return err("Failed opening %s", path);
  errno = 0;
  int sz = fread(buf, 1, BUF_SIZE, f);
  if(errno) {
    err("Failed reading %s", path);
    fclose(f);
    return -1;
  }
  fclose(f);
  buf[sz] = 0;
  return sz;
}

/*    way/
 * If there is a zero somewhere in the first few bytes of
 * the file it's probably binary
 */
int looksBinary(int sz, char* buf) {
  int chk = sz > 256 ? 256 : sz;
  for(int i = 0;i < chk;i++) if(!buf[i]) return 1;
  return 0;
}

#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
void* fl_grep(char* name, void* ctx) {
  struct fl_ctx* fl_ctx = ctx;

  int sz = readFile(name, fl_ctx->buf);
  if(sz < 0) return (void*)sz;
  if(sz == 0) return 0;

  if(looksBinary(sz, fl_ctx->buf)) return 0;

  return (void*)grep(fl_ctx->inVert, sz, fl_ctx->buf, &fl_ctx->rx, name);
}

/*    way/
 * validate the regular expression, then set up
 * the threads, find the files, and give them
 * to the threads to grep.
 */
int search(struct config *config) {
  regex_t rx;
  int e = rxC(config, &rx);
  if(e) return err("Bad regular expression '%s'", config->expr);
  regfree(&rx);

  struct fl_threads* fl_threads = fl_threads_new();
  findFiles(config->cwd, fl_threads);
  return fl_threads_run(fl_threads, fl_grep, config);
}

/*    way/
 * we read the input line-by-line, and check for regular expression
 * on it.
 */
int searchPipe(struct config *config) {
  regex_t rx;
  int e = rxC(config, &rx);
  if(e) return err("Bad regular expression '%s'", config->expr);

  char buf[BUF_SIZE];
  int c = config->inVert ? REG_NOMATCH : 0;
  while(fgets(buf, BUF_SIZE, stdin) != NULL) if(regexec(&rx, buf, 0, 0, 0) == c) printf("%s", buf);

  regfree(&rx);
  return 0;
}

/*    understand/
 * if the input is not a terminal and is
 * a FIFO (cat file | gg) or regular file (gg < file)
 */
int isPipe() {
  if(isatty(fileno(stdin))) return 0;
  struct stat stats;
  fstat(fileno(stdin), &stats);
  return S_ISFIFO(stats.st_mode) || S_ISREG(stats.st_mode);
}

/*    way/
 * show help if needed or do the search
 */
int main(int argc, char* argv[]) {
  struct config config = getConfig(argc, argv);
  if(config.help) return showHelp();
  else if(isPipe()) return searchPipe(&config);
  else return search(&config);
}

