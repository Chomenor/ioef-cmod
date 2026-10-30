# Use the official Ubuntu base image
FROM --platform=linux/amd64 ubuntu:latest as downloader

# Set the working directory
WORKDIR /usr/local/bin

# Install wget to download the binary
RUN apt-get update && apt-get install -y wget unzip

# Download the binary
RUN wget "https://github.com/Chomenor/ioef-cmod/releases/latest/download/ioEF-cMod_v1.29_linux_x86_64.zip"

# Unzip the binary
RUN unzip ioEF-cMod_v1.29_linux_x86_64.zip

# Stage 2: Copy the necessary files to the final image
FROM --platform=linux/amd64 ubuntu:latest

# Set the working directory
WORKDIR /usr/local/bin

# Copy the binary from the downloader stage
COPY --from=downloader /usr/local/bin/cmod_dedicated.x86_64 /usr/local/bin/cmod_dedicated.x86_64

# Give execute permissions to the binary
RUN chmod +x /usr/local/bin/cmod_dedicated.x86_64

# Expose the port
EXPOSE 27960

CMD ["./cmod_dedicated.x86_64", "+set", "net_ip", "0.0.0.0", "+exec", "startup.cfg"]