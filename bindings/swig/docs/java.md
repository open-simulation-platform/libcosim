# libcosim for Java

Best effort: the Java bindings are generated from the same interface as the
other languages, but maintained with lower priority than Python, C and Julia.

## Get it

Download `libcosim-<version>.jar` from the libcosim GitHub release and put
it on the class path. It contains the native libraries for Linux and
Windows and loads them on first use; nothing needs to be on
`java.library.path` or `PATH`. Java 21 or later.

To use it from Maven or Gradle, install it into the local repository under
a group and artifact ID of your choice (the JAR is not published to Maven
Central):

```sh
mvn install:install-file -Dfile=libcosim-<version>.jar \
    -DgroupId=<group> -DartifactId=libcosim -Dversion=<version> -Dpackaging=jar
```

## Example

```java
import libcosim.*;

public class Example {
    static long reference(execution exe, int sim, String name) {
        for (variable_description v : exe.get_model_description(sim).getVariables()) {
            if (v.getName().equals(name)) return v.getReference();
        }
        throw new IllegalArgumentException("no variable " + name);
    }

    public static void main(String[] args) {
        cosim.set_output_level(level.warning);
        execution exe = new execution(0, new fixed_step_algorithm(cosim.to_nanoseconds(0.1), 0));
        int sim = cosim.add_fmu(exe, "identity.fmu", "id", 0, null);

        last_value_observer obs = new last_value_observer();
        exe.add_observer(obs);
        exe.set_real_initial_value(sim, reference(exe, sim, "realIn"), 2.5);
        exe.simulate_until(cosim.to_nanoseconds(1.0));

        DoubleVector values = obs.get_real(sim, new ValueReferenceVector(new long[] {reference(exe, sim, "realOut")}));
        System.out.println(cosim.to_seconds(exe.current_time()) + " s: " + values.get(0));

        try {
            cosim.load_execution_from_ssp("missing.ssp", new execution_load_options());
        } catch (CosimException e) {
            System.out.println(e.getCode() + ": " + e.getMessage());
        }
    }
}
```

## Notes

- Package `libcosim`; the functions and classes have libcosim's C++ names.
  Free functions are static methods of the class `cosim`; enumerations are
  Java enums (`variable_type.real`, `execution_state.running`).
- Times are `long` nanoseconds (`cosim.to_nanoseconds`, `cosim.to_seconds`).
- libcosim errors throw `libcosim.CosimException` (`getCode()`: libcosim's
  error code), an unchecked exception.
- Objects are released by the garbage collector.
