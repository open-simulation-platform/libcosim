## FMI 2 independent-causality migration

Applications that relied on FMI 2 independent variables being reported as
`local` should update their causality filters to include `independent` where
appropriate. The default file observer now logs explicitly declared
independent variables, including time; it continues to exclude local variables.

## ProxyFMU follow-up

In the separate ProxyFMU project:

- Add a regression test using an FMI 2 FMU that declares both an independent
  time variable and a local variable. Check that direct FMU import and the
  proxy FMU and instance model descriptions preserve `independent` and `local`
  respectively, and that time can be read through the proxy during simulation.
- Review client examples, documentation, and downstream causality filters.
  Update any that treat independent variables as local or exclude them on that
  assumption; document the distinction for consumers.

The current ProxyFMU FMI 2 importer already obtains a causality string from
FMI Library. The proxy client loads the model description locally and passes it
to the proxy instance; it does not fetch that metadata over Thrift. The
libcosim ProxyFMU importer now maps both values distinctly. No upstream
conversion or protocol change is known to be required unless the follow-up
tests or consumer review expose a problem.
