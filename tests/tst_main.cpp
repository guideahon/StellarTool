#include <QCoreApplication>

void runTestDiffEngine(int &failures, int argc, char **argv);
void runTestMergeEngine(int &failures, int argc, char **argv);
void runTestUpdateService(int &failures, int argc, char **argv);
void runTestBuilderUi(int &failures, int argc, char **argv);
void runTestCnsConverter(int &failures, int argc, char **argv);
void runTestCnsIdFixer(int &failures, int argc, char **argv);
void runTestHeadless(int &failures, int argc, char **argv);
void runTestOodle(int &failures, int argc, char **argv);
void runTestLiveService(int &failures, int argc, char **argv);

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char fixerLog[] = "test_cns_id_fixer.txt,txt";
        char *args[] = {arg0, argOut, fixerLog};
        runTestCnsIdFixer(failures, 3, args);
    }
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char cnsLog[] = "test_cns_converter.txt,txt";
        char *args[] = {arg0, argOut, cnsLog};
        runTestCnsConverter(failures, 3, args);
    }
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
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char updateLog[] = "test_update.txt,txt";
        char *args[] = {arg0, argOut, updateLog};
        runTestUpdateService(failures, 3, args);
    }
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char builderLog[] = "test_builder_ui.txt,txt";
        char *args[] = {arg0, argOut, builderLog};
        runTestBuilderUi(failures, 3, args);
    }
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char headlessLog[] = "test_headless.txt,txt";
        char *args[] = {arg0, argOut, headlessLog};
        runTestHeadless(failures, 3, args);
    }
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char oodleLog[] = "test_oodle.txt,txt";
        char *args[] = {arg0, argOut, oodleLog};
        runTestOodle(failures, 3, args);
    }
    {
        char arg0[] = "StellarToolTests";
        char argOut[] = "-o";
        char liveLog[] = "test_live.txt,txt";
        char *args[] = {arg0, argOut, liveLog};
        runTestLiveService(failures, 3, args);
    }
    return failures;
}
