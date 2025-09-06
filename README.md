# Warren's LinkedIn Articles

- email: warren.jitsing@infcon.co.za

[viewer under construction]

## Accelerate First. Accelerate Hard. No Latency.

This repository provides a complete, high-performance, and reproducible development environment for C++, Python, and Rust, built using Docker. It also serves as a collection of long-form technical articles, complete with a self-hosted, interactive viewer.

The core philosophy is to provide a professional, first-principles-based workflow that eliminates the "it works on my machine" problem.

## Quick Start 🚀

To get the entire environment up and running, follow these steps from your terminal:

1.  **Clone the Repository**
    ```shell
    git clone https://github.com/warrenjitsing/LinkedInArticles.git
    cd LinkedInArticles
    ```
2.  **Build the Docker Image**
    This script will build your custom development environment. The initial build is a one-time process and may take a significant amount of time as it compiles GCC and Python from source.
    ```shell
    bash build-dev.sh
    ```
3.  **Run the Container**
    This script starts the container with all services running in the background.
    ```shell
    bash dev-container.sh
    ```
4.  **Access Your Services**
      * **SSH / IDE Connection**: Connect to the container's shell or your remote IDE.
        ```shell
        ssh -p 10200 your_user@127.0.0.1
        ```
      * **JupyterLab**: Access the web IDE at `https://127.0.0.1:10201`
      * **Article Viewer**: Access the docs server at `https://127.0.0.1:10202`

-----

## Repository Structure 🗺️

The repository is organized into several distinct, top-level directories, each with a specific purpose.

### Core Environment Files (Root `/`)

The files in the root directory (`Dockerfile`, `build-dev.sh`, `dev-container.sh`, `entrypoint.sh`) define and control the entire containerized environment. They are the "engine" of this repository.

**For a complete, first-principles deconstruction of how these files work, please read the article located in `articles/0002_docker_dev_environment/`.** Understanding this article is key to customizing and maintaining the environment.

### `articles/`

This directory contains the source content for the technical articles. Each sub-directory (e.g., `0001_*`, `0002_*`) represents a single, self-contained article, typically written in Markdown (`README.md`).

### `viewer/`

This directory contains the source code for the FastAPI and JavaScript application that serves and renders the content from the `articles/` directory. It is the web server that provides the interactive reading experience.

### `repos/`

This is a **persistent, mounted workspace** intended for your own projects. You can clone any Git repository into this directory on your host machine, and it will be instantly available inside the container at `/home/your_user/repos`. This is the ideal location for your active development work.

### `data/`

This is a **persistent, mounted storage directory** for miscellaneous files, credentials, and generated assets. Any data you need to persist between container rebuilds should be stored here. By default, it is used for:

  * Importing your `.gitconfig` and GPG keys.
  * Storing the auto-generated, self-signed TLS certificates for the JupyterLab and viewer services.