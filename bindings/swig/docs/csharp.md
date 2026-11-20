# libcosim for C#

Best effort: the C# bindings are generated from the same interface as the
other languages, but maintained with lower priority than Python, C and Julia.

## Get it

Download `Libcosim.<version>.nupkg` from the libcosim GitHub release into a
directory and use it as a package source:

```sh
dotnet nuget add source /path/to/packages --name libcosim-local
dotnet add package Libcosim --version <version>
```

The package targets .NET 8 or later and contains the native libraries for
Linux (`linux-x64`) and Windows (`win-x64`).

## Example

```csharp
using System;
using System.Linq;
using Libcosim;

static class Example
{
    static uint Reference(execution exe, int sim, string name) =>
        exe.get_model_description(sim).variables.First(v => v.name == name).reference;

    static void Main()
    {
        cosim.set_output_level(level.warning);
        var exe = new execution(0, new fixed_step_algorithm(cosim.to_nanoseconds(0.1), 0));
        int sim = cosim.add_fmu(exe, "identity.fmu", "id", 0, null);

        var obs = new last_value_observer();
        exe.add_observer(obs);
        exe.set_real_initial_value(sim, Reference(exe, sim, "realIn"), 2.5);
        exe.simulate_until(cosim.to_nanoseconds(1.0));

        var values = obs.get_real(sim, new ValueReferenceVector(new[] { Reference(exe, sim, "realOut") }));
        Console.WriteLine($"{cosim.to_seconds(exe.current_time())} s: {values[0]}");

        try {
            cosim.load_execution_from_ssp("missing.ssp", new execution_load_options());
        } catch (CosimException e) {
            Console.WriteLine($"{e.Code}: {e.Message}");
        }
    }
}
```

## Notes

- Namespace `Libcosim`; the functions and classes have libcosim's C++ names.
  Free functions are static methods of the class `cosim`; enumerations are
  C# enums (`variable_type.real`, `execution_state.running`).
- Times are `long` nanoseconds (`cosim.to_nanoseconds`, `cosim.to_seconds`).
- libcosim errors throw `Libcosim.CosimException` (`Code`: libcosim's error
  code).
- Objects are released by the garbage collector; they also implement
  `IDisposable`.
