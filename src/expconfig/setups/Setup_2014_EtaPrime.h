
#include "ExpConfig.h"
#include "detectors/CB.h"
#include "detectors/TAPS.h"
#include "detectors/Trigger.h"
#include "unpacker/UnpackerAcqu.h"

#include "base/std_ext.h"

#include "reconstruct/Reconstruct_traits.h"
#include "calibration/modules/EnergyInvariantMass.h"

namespace ant {
namespace expconfig {
namespace setup {

class Setup_2014_EtaPrime :
    public ExpConfig::Module,
    public ExpConfig::Reconstruct,
    public UnpackerAcquConfig
{
public:
  Setup_2014_EtaPrime() {
    detectors.push_back(std::make_shared<detector::Trigger>());
    detectors.push_back(std::make_shared<detector::CB>());
    detectors.push_back(std::make_shared<detector::TAPS_2013>(false)); // no Cherenkov
    calibrations.push_back(std::make_shared<calibration::EnergyInvariantMass>());
  }

  virtual std::list< std::shared_ptr< CalibrationApply_traits > > GetCalibrations() const override {
    return calibrations;
  }

  virtual std::list< std::shared_ptr< Updateable_traits > > GetUpdateables() const override {

    // calibrations and detectors may be updateable
    // so search the list for those kind of objects

    std::list< std::shared_ptr< Updateable_traits > > updateables;
    for(const auto& detector : detectors) {
      const auto& ptr = dynamic_pointer_cast<Updateable_traits, Detector_t>(detector);
      if(ptr == nullptr)
        continue;
      updateables.push_back(ptr);
    }
    for(const auto& calibration : calibrations) {
      const auto& ptr = dynamic_pointer_cast<Updateable_traits, CalibrationApply_traits>(calibration);
      if(ptr == nullptr)
        continue;
      updateables.push_back(ptr);
    }
    return updateables;
  }

  bool Matches(const THeaderInfo& header) const override {
    // check that all detectors match
    for(const auto& detector : detectors) {
      const auto& ptr = dynamic_pointer_cast<ExpConfig::Base, Detector_t>(detector);
      if(ptr == nullptr)
        continue;
      if(!ptr->Matches(header))
        return false;
    }
    /// \todo Make beamtime match stricter than just detectors
    return true;

  }

  void BuildMappings(std::vector<hit_mapping_t>& hit_mappings,
                     std::vector<scaler_mapping_t>& scaler_mappings) const
  {
    // this setup simply asks its underlying
    // detectors for the mappings
    for(const auto& detector : detectors) {
      const UnpackerAcquConfig* cfg
          = dynamic_cast<const UnpackerAcquConfig*>(detector.get());
      if(cfg == nullptr)
        continue;
      cfg->BuildMappings(hit_mappings, scaler_mappings);
    }
    // you may tweak the mapping at this location here
    // for example, ignore elements
  }

private:
  std::list< std::shared_ptr<Detector_t> > detectors;
  std::list< std::shared_ptr<CalibrationApply_traits> > calibrations;
};

}}} // namespace ant::expconfig::setup
