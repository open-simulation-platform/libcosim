package libcosim;

/** A libcosim error (cosim::error), carrying its cosim::errc code. */
public class CosimException extends RuntimeException {
    private final int code;

    public CosimException(String message, int code) {
        super(message);
        this.code = code;
    }

    /** The cosim::errc value (see errc in the generated cosim class). */
    public int getCode() {
        return code;
    }
}
