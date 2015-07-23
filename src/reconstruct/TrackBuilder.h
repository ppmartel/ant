#ifndef ANT_TRACKBUILDER_H
#define ANT_TRACKBUILDER_H

#include "expconfig/Detector_t.h"
#include "tree/TCluster.h"
#include "tree/TEvent.h"

#include <map>
#include <list>
#include <memory>

namespace ant {

namespace expconfig {
namespace detector {
class CB;
class PID;
}
}


namespace reconstruct {

class TrackBuilder {
protected:
    std::shared_ptr<expconfig::detector::CB>  cb;
    std::shared_ptr<expconfig::detector::PID> pid;

public:

    using sorted_detectors_t = std::map<Detector_t::Type_t, std::shared_ptr<Detector_t> >;

    TrackBuilder(const sorted_detectors_t& sorted_detectors);
    virtual ~TrackBuilder() = default;

    // this method shall fill the TEvent reference
    // with tracks built from the given sorted clusters
    /// \todo make this method abstract and create proper derived track builders
    virtual void Build(
            std::map<Detector_t::Type_t, std::list< TCluster > >&& sorted_clusters,
            TEvent::tracks_t& tracks
            );
};

}} // namespace ant::reconstruct

#endif // ANT_TRACKBUILDER_H
