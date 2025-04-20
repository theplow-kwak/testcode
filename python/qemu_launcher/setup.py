from setuptools import setup, find_packages

setup(
    name="qemu-launcher",
    version="0.1.0",
    description="A modular CLI tool to generate and run QEMU virtual machines",
    author="Your Name",
    packages=find_packages(),
    entry_points={"console_scripts": ["qemu-launcher=qemu_launcher.main:main"]},
    install_requires=[],
    python_requires=">=3.8",
    classifiers=["Programming Language :: Python :: 3", "Operating System :: POSIX :: Linux", "License :: OSI Approved :: MIT License", "Development Status :: 4 - Beta"],
    include_package_data=True,
    zip_safe=False,
)
