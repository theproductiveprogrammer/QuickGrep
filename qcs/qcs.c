#include<stdio.h>
#include<string.h>

int p(char *msg) { return printf("%s\n", msg); }

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

int search(struct config config) {
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

