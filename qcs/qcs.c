#include<stdio.h>
#include<string.h>
#include<ftw.h>
#include<stdint.h>

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

static int ignore_base = 0;

static int searchFile(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
  if(tflag == FTW_D || tflag == FTW_DNR) {
    if(strcmp(".git", fpath+ftwbuf->base) == 0) ignore_base = ftwbuf->base;
  }
  if(ignore_base && ftwbuf->base < ignore_base) ignore_base = 0;
  if(tflag != FTW_F) return 0;
  if(!S_ISREG(sb->st_mode)) return 0;
  if(sb->st_size > 1024 * 1024) printf("Ignoring %s\n", fpath);
  if(sb->st_size > 1024 * 1024) return 0;
  if(strncmp(".git", fpath + ignore_base, 4) == 0) printf("Ignoring %s\n", fpath);
  /*
    printf("%-3s %2d %7jd   %-40s %d %s\n",
        (tflag == FTW_D) ?   "d"   : (tflag == FTW_DNR) ? "dnr" :
        (tflag == FTW_DP) ?  "dp"  : (tflag == FTW_F) ?
            (S_ISBLK(sb->st_mode) ? "f b" :
             S_ISCHR(sb->st_mode) ? "f c" :
             S_ISFIFO(sb->st_mode) ? "f p" :
             S_ISREG(sb->st_mode) ? "f r" :
             S_ISSOCK(sb->st_mode) ? "f s" : "f ?") :
        (tflag == FTW_NS) ?  "ns"  : (tflag == FTW_SL) ?  "sl" :
        (tflag == FTW_SLN) ? "sln" : "?",
        ftwbuf->level, (intmax_t) sb->st_size,
        fpath, ftwbuf->base, fpath + ftwbuf->base);*/
    return 0;            
}


int search(struct config config) {
  int r = nftw(".", searchFile, 20, 0);
  if(r == -1) return err("Failed walking directory", NULL);
  else return r;
}

/*    way/
 * show help if needed or do the search
 */
int main(int argc, char* argv[]) {
  struct config config = getConfig(argc, argv);
  if(config.help) return showHelp();
  else return search(config);
}

