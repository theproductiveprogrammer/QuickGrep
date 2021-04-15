#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include<fts.h>
#include<errno.h>

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

struct config {
  int help;
  int ignorecase;
  char expr[1024];
};

struct config getConfig(int argc, char* argv[]) {
  struct config r;
  r.help = argc < 2;

  char* expr = r.expr;
  for(int i = 1;i < argc;i++) {
    expr = stpcpy(expr, argv[i]);
    *expr = ' ';
    expr++;
  }

  r.ignorecase = 1;
  for(expr = r.expr;*expr;expr++) {
    if(*expr >= 'A' && *expr <= 'Z') r.ignorecase = 0;
  }

  return r;
}

int showHelp() {
  p("qcs: Quick Code Search");
  p("$> qcs <regular expression>");
  p("      Finds all matches in code/text files");
  p("      Uses sMartCase (ignores case by default)");

  return 0;
}

int fts_err(FTSENT *node) {
  if(node->fts_info == FTS_DNR ||
      node->fts_info == FTS_ERR ||
      node->fts_info == FTS_NS) return node->fts_errno;
  return 0;
}

#define MAX_SIZE (1024 * 1024)

int skip(FTSENT *node) {
  if(node->fts_info == FTS_F) {
    return node->fts_statp->st_size > MAX_SIZE;
  }
  if(strcmp(".git", node->fts_name) == 0) return 1;
  if(strcmp("node_modules", node->fts_name) == 0) return 1;
  return 0;
}

static char buf[MAX_SIZE];
int grep(char *path, char *currpath) {

  FILE *f = fopen(currpath, "r");
  if(!f) return err("Failed opening %s", path);
  errno = 0;
  int sz = fread(buf, 1, MAX_SIZE, f);
  if(errno) {
    return err("Failed reading %s", path);
    fclose(f);
  }
  fclose(f);
  buf[sz] = 0;

  int chk = sz > 256 ? 256 : sz;
  for(int i = 0;i < sz;i++) if(!buf[i]) return 0;

  return 0;
}

int search(struct config config) {
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
        grep(node->fts_path, node->fts_accpath);
      }
    }
  }
  fts_close(tree);
  if(errno) return err("Failed walking directory", NULL);

  return 0;
}

/*    way/
 * show help if needed or do the search
 */
int main(int argc, char* argv[]) {
  struct config config = getConfig(argc, argv);
  if(config.help) return showHelp();
  else return search(config);
}

