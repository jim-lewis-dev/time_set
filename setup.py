"""Build the shared C++ engine, including the CLI, as a native Python extension."""
import os
from setuptools import Extension, find_packages, setup

flags = ["/std:c++20", "/EHsc"] if os.name == "nt" else ["-std=c++20", "-Wall", "-Wextra", "-Wpedantic"]
setup(
    name="time_set",
    version="0",  # Required packaging metadata, not a release/versioned API.
    description="Exact, finite interval sets with C++, Python, and command-line interfaces",
    author="Jim Lewis",
    license="MIT",
    python_requires=">=3.10",
    package_dir={"": "python"},
    packages=find_packages("python"),
    package_data={"time_set": ["py.typed"]},
    ext_modules=[Extension(
        "time_set._native",
        sources=["src/python_native.cpp", "src/time_set_io.cpp", "src/cli.cpp"],
        include_dirs=["include", "src"],
        depends=["include/time_set.hpp", "include/time_set_io.hpp", "include/time_event.hpp", "src/cli.hpp"],
        language="c++",
        extra_compile_args=flags,
    )],
    entry_points={"console_scripts": ["time_set=time_set:main"]},
)
