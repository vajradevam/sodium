#pragma once

#include <string>
#include <vector>

struct PreprocessedResult {
    std::string expanded_source;
    std::vector<std::string> included_files; // all files that were processed
};

// Preprocess source text: resolve #include directives, insert #line markers.
// include_dirs: additional search paths (from -I flags).
// system_include_dir: default system include path (e.g. /usr/local/include/cyan).
PreprocessedResult preprocess(
    const std::string& source_text,
    const std::string& source_filename,
    const std::vector<std::string>& include_dirs,
    const std::string& system_include_dir = ""
);
