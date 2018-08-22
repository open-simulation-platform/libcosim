# escape=`

# Use the latest Windows Server Core image with .NET Framework 4.7.1.
FROM microsoft/dotnet-framework:4.7.1

# Restore the default Windows shell for correct batch processing below.
SHELL ["cmd", "/S", "/C"]

# Download the Build Tools bootstrapper.
ADD https://aka.ms/vs/15/release/vs_buildtools.exe C:\TEMP\vs_buildtools.exe

# Install Build Tools excluding workloads and components with known issues.
RUN C:\TEMP\vs_buildtools.exe --quiet --wait --norestart --nocache `
    --installPath C:\BuildTools `
    --add Microsoft.VisualStudio.Workload.VCTools `
 || IF "%ERRORLEVEL%"=="3010" EXIT 0

# Download and install CMake
ADD https://cmake.org/files/v3.12/cmake-3.12.1-win64-x64.msi C:\TEMP\cmake-3.12.1-win64-x64.msi
RUN C:\TEMP\cmake-3.12.1-win64-x64.msi /quiet ADD_CMAKE_TO_PATH=System

# Downaload and install Conan
ADD https://dl.bintray.com/conan/installers/conan-win-64_1_6_1.exe C:\TEMP\conan-win-64_1_6_1.exe
RUN C:\TEMP\conan-win-64_1_6_1.exe /silent

VOLUME C:\conan-repo
ENV CONAN_USER_HOME=C:\conan-repo
ENV CONAN_USER_HOME_SHORT=none

# Download and install Doxygen
ADD http://ftp.stack.nl/pub/users/dimitri/doxygen-1.8.14-setup.exe C:\TEMP\doxygen-1.8.14-setup.exe
RUN C:\TEMP\doxygen-1.8.14-setup.exe /silent

# Start developer command prompt with any other commands specified.
ENTRYPOINT C:\BuildTools\Common7\Tools\VsDevCmd.bat &&

# Default to PowerShell if no other command specified.
CMD ["powershell.exe", "-NoLogo", "-ExecutionPolicy", "Bypass"]
