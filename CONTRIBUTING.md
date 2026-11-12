# Contributing

Thanks for wanting to help. Nexus-CORE is a preview made by one
person, so the most useful help right now is **testing and reporting**.

## Reporting a problem

Open an [issue](https://github.com/DAVIDOSDAS-official/nexus-core/issues)
with:

- what you did, what you expected, and what happened;
- the edition (KDE or Minimal) and `nexus --version`;
- the output of `nexus doctor`;
- your hardware, if it might matter (laptop model, graphics card).

Security problems go through [SECURITY.md](SECURITY.md), not a public
issue.

## Ideas

Ideas are welcome as issues too. Nexus has a few rules that every
change has to keep:

- **Nothing changes without `--apply`.** Every other command only
  reads and explains.
- **Before a change, say what will happen and what will not.**
- **Every change is written to `nexus history`.**
- **No accounts and no tracking.**
- **Fedora's tools do the work**; Nexus decides and explains.

## Code

```
cmake -S . -B build && cmake --build build
ctest --test-dir build
```

All tests must pass. A pull request should say what it changes, why,
and how it was tested; on real hardware is best. GitHub runs the tests
for every pull request, once as built for people and once with the
sanitizers on (`.github/workflows/tests.yml`). To do the second
yourself:

```
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -O1"
cmake --build build-san -j2 && ctest --test-dir build-san
./build-san/tests/parser_fuzz 10
```

A fix for a security problem comes with a test that fails without it.
A new command that changes the system goes into
`tests/no-apply-changes-nothing.sh`; a new parser of outside text goes
into `tests/fuzz/parser_fuzz.cpp`.

Install `strace` (`sudo apt install strace`) and the `--apply` test is
the full one: it then sees every program a preview starts and every
file it writes. A new tool that nexus runs has to be classified there:
a stand-in in the script if it can change something, or a line in the
read-only list in `tests/no-apply-trace.py` if it only looks.

Two tests need root and a machine that can be thrown away (they write
a pretend server into `/etc/containers/systemd`); they are skipped
otherwise, and GitHub runs them:

```
sudo NEXUS_TEST_ALLOW_ROOT=1 ctest --test-dir build -R 'ServerBackup|NoApply'
```

By contributing you agree that your contribution is licensed under
the GPL-3.0, like the rest of the code.
