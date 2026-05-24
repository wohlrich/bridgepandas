from setuptools import setup, Extension

setup(
    ext_modules=[
        Extension(
            name="bridgepandas.jbdd",
            sources=["jbdd/jbdd.cpp", "jbdd/j128.cpp"],
            include_dirs=["jbdd"],
            extra_compile_args=["-std=c++17"],
            language="c++",
        )
    ]
)
