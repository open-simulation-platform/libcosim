namespace Libcosim
{
    /// <summary>A libcosim error (cosim::error), carrying its cosim::errc code.</summary>
    public class CosimException : System.ApplicationException
    {
        public CosimException(string message, int code) : base(message)
        {
            Code = code;
        }

        /// <summary>The cosim::errc value (see the generated errc enum).</summary>
        public int Code { get; }
    }
}
