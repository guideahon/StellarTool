#include <QCoreApplication>

void runTestDiffEngine(int &failures, int argc, char **argv);
void runTestMergeEngine(int &failures, int argc, char **argv);

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char diffLog[] = "test_diff.txt,txt";
        char *args[] = {arg0, argOut, diffLog};
        runTestDiffEngine(failures, 3, args);
    }
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char mergeLog[] = "test_merge.txt,txt";
        char *args[] = {arg0, argOut, mergeLog};
        runTestMergeEngine(failures, 3, args);
    }
    return failures;
}
