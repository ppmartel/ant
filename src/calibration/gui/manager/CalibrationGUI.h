#pragma once

#include "GUIInterface.h"
#include "AvgBuffer.h"

#include "base/interval.h"
#include "tree/TDataRecord.h"

#include <memory>
#include <list>
#include <vector>
#include <string>

class TH1D;
class TH2D;
class TFile;
class TQObject;

namespace ant {
namespace calibration {
namespace gui {

class CalibrationGUI {
protected:

    using myBuffer_t = ant::calibration::gui::AvgBuffer<TH2D,ant::interval<ant::TID>>;

    std::unique_ptr<GUIClientInterface> module;
    myBuffer_t buffer;

    struct input_file_t {

        input_file_t(const std::string& FileName, const ant::interval<ant::TID>& R):
            filename(FileName),
            range(R) {}

        bool operator< (const input_file_t& other) const;

        const std::string filename;
        const ant::interval<ant::TID> range;
    };

    std::list<input_file_t> input_files;


    struct state_t {
        bool is_init;

        std::list<input_file_t>::iterator file;
        myBuffer_t::const_iterator buffpos;
        unsigned channel;

        bool break_occured;
        bool finish_mode;
    };

    state_t state;

    std::list<input_file_t> ScanFiles(const std::vector<std::string> filenames);

    void ProcessFile(input_file_t &file_input);
    void ProcessModules();


public:
    enum class RunReturnStatus_t {
        Continue,
        Stop
    };

    CalibrationGUI(std::unique_ptr<GUIClientInterface> module_,
                   unsigned length);

    virtual void ConnectReturnFunc(const char* receiver_class, void* receiver, const char* slot);

    virtual void SetFileList(const std::vector<std::string>& filelist);

    virtual bool Run();

    virtual ~CalibrationGUI();

};

}
}
}
