set -euo pipefail

tests=$(ls tests/*.cpp)
for t in $tests; do
    executable=$(basename $t .cpp)
    ./$executable > tests/$executable.out
    if [ -f "./tests/$executable.out.correct" ]; then
        diff -u --color=auto tests/$executable.out tests/$executable.out.correct
        printf "Test $executable passed!\n"
    else
        printf "Test $executable has no .out.correct file, skipping\n"
    fi
done