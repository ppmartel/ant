#pragma once

#include "base/interval.h"
#include "tree/THeaderInfo.h"
#include "base/WrapTFile.h"

#include <string>
#include <list>
#include <memory>

class TH1;
class TQObject;

namespace ant {
namespace calibration {
namespace gui {

class CalCanvas;

class ManagerWindow_traits {
public:
    virtual gui::CalCanvas* AddCalCanvas(const std::string& name = "") =0;
    virtual void AddCheckBox(const std::string& label, bool& flag) =0;
    virtual void AddNumberEntry(const std::string& label, double& number) =0;
};

class CalibModule_traits {
private:
    const std::string name;
public:
    CalibModule_traits(const std::string& name_) :
        name(name_) {}
    virtual ~CalibModule_traits() {}
    virtual std::string GetName() const { return name; }

    virtual std::shared_ptr<TH1> GetHistogram(const WrapTFile& file) const =0;
    virtual unsigned GetNumberOfChannels() const =0;

    virtual void InitGUI(gui::ManagerWindow_traits* window) =0;

    virtual void StartSlice(const interval<TID>& range) =0;

    enum class DoFitReturn_t {
        Next, Display, Skip
    };
    virtual DoFitReturn_t DoFit(TH1* hist, unsigned channel) =0;
    virtual void DisplayFit() =0;
    virtual void StoreFit(unsigned channel) =0;

    virtual bool FinishSlice() =0;
    virtual void StoreFinishSlice(const interval<TID>& range) =0;
};


}
}
}
