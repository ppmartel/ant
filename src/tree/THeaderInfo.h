#pragma once

#include "TDataRecord.h"

#ifndef __CINT__
#include "base/std_ext/time.h"
#endif

namespace ant {

struct THeaderInfo : TDataRecord
{

  THeaderInfo(
      const TID& id,
      std::time_t timestamp,
      const std::string& description,
      unsigned runnumber
      )
    :
      TDataRecord(id),
      SetupName(),
      Timestamp(timestamp),
      RunNumber(runnumber),
      Description(description)
  {
    static_assert(sizeof(decltype(timestamp)) <= sizeof(decltype(Timestamp)),
                  "Bug: type of timestamp too big for THeaderInfo"
                  );
    static_assert(sizeof(runnumber) <= sizeof(decltype(RunNumber)),
                  "Bug: type of runnumber too big for THeaderInfo"
                  );
  }

  THeaderInfo(
      const TID& id,
      const std::string& setupName
      )
    :
      TDataRecord(id),
      SetupName(setupName),
      Timestamp(),
      RunNumber(),
      Description()
  {}


  std::string   SetupName;   // specify expconfig setup manually (if non empty)
  std::int64_t  Timestamp;   // unix epoch
  std::uint32_t RunNumber;   // runnumber
  std::string   Description; // full descriptive string

#ifndef __CINT__
  virtual std::ostream& Print( std::ostream& s) const override {
    s << "THeaderInfo ID=" << ID;
    if(SetupName.empty()) {
        s << " Timestamp='" << std_ext::to_iso8601(Timestamp) << "'"
          << " RunNumber=" << RunNumber
          << " Description='" << Description << "'";
    }
    else
        s << " SetupID=" << SetupName;
    return s;
  }
#endif

  THeaderInfo() :  TDataRecord(), SetupName(), Timestamp(), RunNumber(), Description() {}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif
  ClassDef(THeaderInfo, ANT_UNPACKER_ROOT_VERSION)
#ifdef __clang__
#pragma clang diagnostic pop
#endif

};

} // namespace ant
