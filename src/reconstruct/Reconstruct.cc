#include "Reconstruct.h"

#include "Clustering.h"

#include "expconfig/ExpConfig.h"

#include "tree/TDataRecord.h"
#include "tree/TDetectorRead.h"
#include "tree/TEvent.h"
#include "TrackBuilder.h"

#include "base/Logger.h"
#include "base/std_ext.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <limits>

using namespace std;
using namespace ant;
using namespace ant::reconstruct;

Reconstruct::Reconstruct(const THeaderInfo &headerInfo)
{
    const auto& config = ExpConfig::Reconstruct::Get(headerInfo);

    // calibrations are the natural Updateable_traits objects,
    // but they could be constant (in case of a very simple calibration)
    calibrations = config->GetCalibrations();
    for(const auto& calibration : calibrations) {
        const auto& ptr = dynamic_pointer_cast<Updateable_traits, CalibrationApply_traits>(calibration);
        if(ptr != nullptr)
            updateables.push_back(ptr);
    }

    // detectors serve different purposes, ...
    auto detectors = config->GetDetectors();
    for(const auto& detector : detectors) {
        // ... they may be updateable (think of the PID Phi angles)
        const auto& updateable = dynamic_pointer_cast<Updateable_traits, Detector_t>(detector);
        if(updateable != nullptr)
            updateables.push_back(updateable);
        // ... but also are needed in DoReconstruct
        /// \todo check if types are unique
        sorted_detectors[detector->Type] = detector;
    }

    // init the trackbuilder
    /// \todo Make use of different TrackBuilders maybe?
    trackbuilder = std_ext::make_unique<TrackBuilder>(sorted_detectors);

    /// \todo build the range list from updateables
}

unique_ptr<TEvent> Reconstruct::DoReconstruct(TDetectorRead& detectorRead)
{

    // categorize the hits by detector type
    // this is handy for all subsequent reconstruction steps
    map<Detector_t::Type_t, list< TDetectorReadHit* > > sorted_readhits;
    for(TDetectorReadHit& readhit : detectorRead.Hits) {
        sorted_readhits[readhit.GetDetectorType()].push_back(addressof(readhit));
    }

    // apply calibration (this may change the given detectorRead!)
    for(const auto& calib : calibrations) {
        calib->ApplyTo(sorted_readhits);
    }

    // for debug purposes, dump out the detectorRead
    //cout << detectorRead << endl;

    // already create the event here, since Tagger
    // doesn't need hit matching and thus can be filled already
    // in BuildHits (see below)
    auto event = std_ext::make_unique<TEvent>(detectorRead.ID);

    // the detectorRead is now calibrated as far as possible
    // lets start the hit matching, which builds the TClusterHit's
    // we also extract the energy, which is always defined as a
    // single value with type Channel_t::Type_t
    sorted_bydetectortype_t<HitWithEnergy_t> sorted_clusterhits;
    BuildHits(move(sorted_readhits), sorted_clusterhits, event->Tagger);

    // then build clusters (at least for calorimeters this is not trivial)
    sorted_bydetectortype_t<TCluster> sorted_clusters;
    BuildClusters(move(sorted_clusterhits), sorted_clusters);


    // finally, do the track building
    trackbuilder->Build(move(sorted_clusters), event->Tracks);

    // uncomment for debug purposes
    //cout << *event << endl;

    return event;
}

Reconstruct::~Reconstruct()
{

}

void Reconstruct::BuildHits(
        sorted_bydetectortype_t<TDetectorReadHit *>&& sorted_readhits,
        sorted_bydetectortype_t<Reconstruct::HitWithEnergy_t>& sorted_clusterhits,
        TTagger& event_tagger)
{
    auto insert_hint = sorted_clusterhits.cbegin();

    for(const auto& it_hit : sorted_readhits) {
        list<HitWithEnergy_t> clusterhits;
        const Detector_t::Type_t detectortype = it_hit.first;
        const auto& readhits = it_hit.second;

        // find the detector instance for this type
        const auto& it_detector = sorted_detectors.find(detectortype);
        if(it_detector == sorted_detectors.end())
            continue;
        const shared_ptr<Detector_t>& detector = it_detector->second;

        // the tagging devices are excluded from further hit matching and clustering
        const shared_ptr<TaggerDetector_t>& taggerdetector
                = dynamic_pointer_cast<TaggerDetector_t>(detector);

        for(const TDetectorReadHit* readhit : readhits) {
            // ignore uncalibrated items
            if(readhit->Values.empty())
                continue;

            // for tagger detectors, we do not match the hits by channel at all
            if(taggerdetector != nullptr) {
                /// \todo handle Integral and Scaler type information here
                if(readhit->GetChannelType() != Channel_t::Type_t::Timing)
                    continue;
                // but we add each TClusterHitDatum as some single
                // TClusterHit representing an electron with a timing
                /// \todo implement tagger double-hit decoding?
                for(const double timing : readhit->Values) {
                    event_tagger.Hits.emplace_back(
                                taggerdetector->GetPhotonEnergy(readhit->Channel),
                                TKeyValue<double>(readhit->Channel, timing)
                                );
                }

                /// \todo add Moeller/PairSpec information here
                continue;
            }

            // transform the data from readhit into TClusterHitDatum's
            vector<TClusterHitDatum> data(readhit->Values.size());
            auto do_transform = [readhit] (double value) {
                return TClusterHitDatum(readhit->GetChannelType(), value);
            };
            transform(readhit->Values.cbegin(), readhit->Values.cend(),
                      data.begin(), do_transform);

            // for non-tagger detectors, search for a TClusterHit with same channel
            const auto match_channel = [readhit] (const HitWithEnergy_t& hit) {
                return hit.Hit.Channel == readhit->Channel;
            };
            const auto it_clusterhit = find_if(clusterhits.begin(),
                                               clusterhits.end(),
                                               match_channel);
            if(it_clusterhit == clusterhits.end()) {
                // not found, create new HitWithEnergy from readhit
                clusterhits.emplace_back(readhit, move(data));
            }
            else {
                // clusterhit with channel of readhit already exists,
                // so append TClusterHitDatum's and try to set energy
                it_clusterhit->MaybeSetEnergy(readhit);
                move(data.begin(), data.end(),
                     back_inserter(it_clusterhit->Hit.Data));
            }
        }

        // The trigger or tagger detectors don't fill anything
        // so skip it
        if(clusterhits.empty())
            continue;

        // insert the clusterhits
        insert_hint =
                sorted_clusterhits.insert(insert_hint,
                                          make_pair(detectortype, move(clusterhits)));
    }
}

void Reconstruct::BuildClusters(
        sorted_bydetectortype_t<Reconstruct::HitWithEnergy_t>&& sorted_clusterhits,
        sorted_bydetectortype_t<TCluster>& sorted_clusters)
{
    auto insert_hint = sorted_clusters.begin();

    for(const auto& it_clusterhits : sorted_clusterhits) {
        const Detector_t::Type_t detectortype = it_clusterhits.first;
        const list<HitWithEnergy_t>& clusterhits = it_clusterhits.second;

        // find the detector instance for this type
        const auto& it_detector = sorted_detectors.find(detectortype);
        if(it_detector == sorted_detectors.end())
            continue;
        const shared_ptr<Detector_t>& detector = it_detector->second;

        list<TCluster> clusters;

        // check if detector can do clustering,
        const shared_ptr<ClusterDetector_t>& clusterdetector
                = dynamic_pointer_cast<ClusterDetector_t>(detector);

        if(clusterdetector != nullptr) {
            // clustering detector, so we need additional information
            // to build the crystals_t
            list<clustering::crystal_t> crystals;
            for(const HitWithEnergy_t& clusterhit : clusterhits) {
                const TClusterHit& hit = clusterhit.Hit;
                // ignore hits without energy information
                if(!isfinite(clusterhit.Energy))
                    continue;
                crystals.emplace_back(
                            clusterhit.Energy,
                            clusterdetector->GetClusterElement(hit.Channel),
                            addressof(hit)
                            );

            }
            // do the clustering
            vector< vector< clustering::crystal_t> > crystal_clusters;
            do_clustering(crystals, crystal_clusters);

            // now calculate some cluster properties,
            // and create TCluster out of it
            for(const auto& cluster : crystal_clusters) {
                const double cluster_energy = clustering::calc_total_energy(cluster);
                /// \todo already cut low-energy clusters here?
                TVector3 weightedPosition(0,0,0);
                double weightedSum = 0;
                std::vector<TClusterHit> clusterhits;
                clusterhits.reserve(cluster.size());
                for(const clustering::crystal_t& crystal : cluster) {
                    double wgtE = clustering::calc_energy_weight(crystal.Energy, cluster_energy);
                    weightedPosition += crystal.Element->Position * wgtE;
                    weightedSum += wgtE;
                    clusterhits.emplace_back(*crystal.Hit);
                }
                weightedPosition *= 1.0/weightedSum;
                clusters.emplace_back(
                            weightedPosition,
                            cluster_energy,
                            detector->Type,
                            clusterhits
                            );
            }
        }
        else {
            // in case of no clustering detector,
            // build "cluster" consisting of single TClusterHit
            for(const HitWithEnergy_t& clusterhit : clusterhits) {
                const TClusterHit& hit = clusterhit.Hit;
                clusters.emplace_back(
                            detector->GetPosition(hit.Channel),
                            clusterhit.Energy,
                            detector->Type,
                            vector<TClusterHit>{hit}
                            );
            }
        }

        // insert the clusters
        insert_hint =
                sorted_clusters.insert(insert_hint,
                                       make_pair(detectortype, move(clusters)));
    }
}


void Reconstruct::HitWithEnergy_t::MaybeSetEnergy(const TDetectorReadHit *readhit) {
    if(readhit->GetChannelType() != Channel_t::Type_t::Integral)
        return;
    if(readhit->Values.size() != 1)
        return;
    if(isfinite(Energy)) {
        LOG(WARNING) << "Found hit in channel with more than one energy information";
        return;
    }
    Energy = readhit->Values[0];
}

Reconstruct::HitWithEnergy_t::HitWithEnergy_t(const TDetectorReadHit *readhit, const vector<TClusterHitDatum> &&data) :
    Hit(readhit->Channel, data),
    Energy(numeric_limits<double>::quiet_NaN())
{
    MaybeSetEnergy(readhit);
}
