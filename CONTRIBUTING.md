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
and how it was tested; on real hardware is best.

By contributing you agree that your contribution is licensed under
the GPL-3.0, like the rest of the code.
