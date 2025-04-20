set -euo pipefail

tests=$(ls tests/*.cpp)
for t in $tests; do
    executable=$(basename $t .cpp)
    printf "Test $executable... "
    ./$executable > tests/$executable.out
    if [ -f "./tests/$executable.out.correct" ]; then
        diff -u --color=auto tests/$executable.out tests/$executable.out.correct
        printf "passed!\n"
    else
        printf "finished successfully but has no .out.correct file, skipping comparison\n"
    fi
done
