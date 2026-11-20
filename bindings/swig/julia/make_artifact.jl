# Packs the native libraries of the Julia package (the C library and its
# dependencies) as the `libcosim_c` artifact for one platform.
#
# Usage: julia make_artifact.jl <native dir> <out dir> <download URL base>
#
# Writes to <out dir>:
#   libcosim_c-<triplet>.tar.gz     the artifact (lib/ = <native dir>)
#   Artifacts-<triplet>.toml        its Artifacts.toml entry, downloading
#                                   from <download URL base>/<tarball name>
# The entries of several platforms concatenate into the package's
# Artifacts.toml. Run with a scratch JULIA_DEPOT_PATH: the artifact is also
# created in the depot.
using Pkg.Artifacts
using Base.BinaryPlatforms

native, outdir, urlbase = ARGS
host = HostPlatform()
platform = Platform(arch(host), os(host))  # no Julia-version or ABI tags
name = "libcosim_c-$(triplet(platform))"
tarball = joinpath(outdir, "$name.tar.gz")
toml = joinpath(outdir, "Artifacts-$(triplet(platform)).toml")

hash = create_artifact() do dir
    cp(native, joinpath(dir, "lib"))
end
mkpath(outdir)
sha256 = archive_artifact(hash, tarball)
rm(toml; force=true)
bind_artifact!(toml, "libcosim_c", hash; platform,
               download_info=[("$(rstrip(urlbase, '/'))/$name.tar.gz", sha256)])
println("artifact libcosim_c for $(triplet(platform)): $hash -> $tarball")
