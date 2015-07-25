#pragma once

#include <memory>
#include <string>

class TFile;

namespace ant {

class WrapTFile {
protected:
    std::unique_ptr<TFile> file;

public:
    WrapTFile(const std::string& filename);

    ///@todo is this required?? remove if possible
    TFile* operator* () { return file.get(); }

    bool isOpen() const;
    void cd();

    ~WrapTFile();
    WrapTFile(const WrapTFile&) = delete;
    WrapTFile& operator= (const WrapTFile&) = delete;
};
}
