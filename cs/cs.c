#include<stdio.h>

int p(char *msg) { return printf("%s\n", msg); }

struct config {
  int help;
};

struct config getConfig(int argc, char* argv[]) {
  struct config r;
  r.help = 1;

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

