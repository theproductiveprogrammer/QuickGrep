#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include<fts.h>
#include<errno.h>
#include<regex.h>

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
  int i = 1;
  while(1) {
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
    for(;s < sz;s++) {
      if(buf[s] == '\n') {
        buf[s] = 0;
        break;
      }
    }
    printf("%s:%d:%s\n", path, lnum, buf+ls);
    if(s < sz) buf[s] = '\n';
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

/*    way/
 * show help if needed or do the search
 */
int main(int argc, char* argv[]) {
  struct config config = getConfig(argc, argv);
  if(config.help) return showHelp();
  else return search(&config);
}

