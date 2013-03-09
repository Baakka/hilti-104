
#ifndef BINPAC_OPTIONS_H
#define BINPAC_OPTIONS_H

#include <hilti/options.h>

namespace binpac {

/// A set of options controlling the compilation and link process. This
/// inherits all the HILTi options and adds further BinPAC++-specific ones.
///
/// For the label lists, BinPAC++ options are prefixed with "binpac".
class Options : public hilti::Options
{
public:
    Options();
    Options(const Options& other) = default;
    virtual ~Options() {};

    /// List of directories to search for imports and other \c *.pac2 library
    /// files. The current directory will always be tried first. By default,
    /// this set is set to the current directory plus the installation-wide
    /// HILTI library directories (in that order).
    string_list libdirs_pac2;

    string_set cgDebugLabels() const override;
    string_set optimizationLabels() const override;
};

}

#endif
