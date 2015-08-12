#include "CB_Energy.h"

#include "calibration/gui/CalCanvas.h"
#include "calibration/fitfunctions/FitGausPol3.h"

#include "analysis/plot/HistogramFactories.h"
#include "analysis/data/Event.h"
#include "analysis/utils/combinatorics.h"

#include "expconfig/detectors/CB.h"

#include "tree/TDataRecord.h"

#include "base/cbtaps_display/TH2CB.h"
#include "base/Logger.h"

#include <list>


using namespace std;
using namespace ant;
using namespace ant::calibration;
using namespace ant::analysis;
using namespace ant::analysis::data;

CB_Energy::CB_Energy(std::shared_ptr<expconfig::detector::CB> cb,
                     std::shared_ptr<DataManager> calmgr,
                     Calibration::Converter::ptr_t converter,
                     double defaultPedestal,
                     double defaultGain,
                     double defaultThreshold,
                     double defaultRelativeGain):
    Energy(cb->Type,
           calmgr,
           converter,
           defaultPedestal,
           defaultGain,
           defaultThreshold,
           defaultRelativeGain),
    cb_detector(cb)
{

}

unique_ptr<analysis::Physics> CB_Energy::GetPhysicsModule()
{
    return std_ext::make_unique<ThePhysics>(GetName(),
                                            Gains.Name,
                                            cb_detector->GetNChannels());
}

void CB_Energy::GetGUIs(list<unique_ptr<gui::Manager_traits> >& guis) {
    guis.emplace_back(std_ext::make_unique<TheGUI>(
                          GetName(),
                          Gains,
                          calibrationManager,
                          cb_detector
                          ));
}


CB_Energy::ThePhysics::ThePhysics(const string& name,
                                  const string& hist_name,
                                  unsigned nChannels):
    Physics(name)
{
    const BinSettings cb_channels(nChannels);
    const BinSettings energybins(1000);

    ggIM = HistFac.makeTH2D("2 neutral IM (CB,CB)", "IM [MeV]", "#",
                            energybins, cb_channels, hist_name);
}

void CB_Energy::ThePhysics::ProcessEvent(const Event& event)
{
    const auto& cands = event.Reconstructed().Candidates();

    for( auto comb = analysis::utils::makeCombination(cands,2); !comb.Done(); ++comb ) {
        const CandidatePtr& p1 = comb.at(0);
        const CandidatePtr& p2 = comb.at(1);

        if(p1->VetoEnergy()==0 && p2->VetoEnergy()==0
           && (p1->Detector() & Detector_t::Type_t::CB)
           && (p2->Detector() & Detector_t::Type_t::CB)) {
            const Particle a(ParticleTypeDatabase::Photon,comb.at(0));
            const Particle b(ParticleTypeDatabase::Photon,comb.at(1));
            const TLorentzVector gg = a + b;

            auto cl1 = p1->FindCaloCluster();
            if(cl1)
                ggIM->Fill(gg.M(),cl1->CentralElement);

            auto cl2 = p2->FindCaloCluster();
            if(cl2)
                ggIM->Fill(gg.M(),cl2->CentralElement);
        }
    }
}

void CB_Energy::ThePhysics::Finish()
{

}

void CB_Energy::ThePhysics::ShowResult()
{
    canvas(GetName()) << drawoption("colz") << ggIM << endc;
}

CB_Energy::TheGUI::TheGUI(const string& basename,
                          CalibType& type,
                          const std::shared_ptr<DataManager>& calmgr,
                          const std::shared_ptr<expconfig::detector::CB>& cb) :
    GUI_CalibType(basename, type, calmgr),
    cb_detector(cb),
    func(make_shared<gui::FitGausPol3>())
{

}

unsigned CB_Energy::TheGUI::GetNumberOfChannels() const
{
    return cb_detector->GetNChannels();
}

void CB_Energy::TheGUI::InitGUI()
{
    c_fit = new gui::CalCanvas("c_fit", GetName()+": Fit");
    c_overview = new gui::CalCanvas("c_overview", GetName()+": Overview");
    h_peaks = new TH1D("h_peaks","Peak positions",GetNumberOfChannels(),0,GetNumberOfChannels());
    h_peaks->SetXTitle("Channel Number");
    h_peaks->SetYTitle("Pi0 Peak / MeV");
    h_relative = new TH1D("h_relative","Relative change from previous gains",GetNumberOfChannels(),0,GetNumberOfChannels());
    h_relative->SetXTitle("Channel Number");
    h_relative->SetYTitle("Relative change / %");
}

list<gui::CalCanvas*> CB_Energy::TheGUI::GetCanvases() const
{
    return {c_fit, c_overview};
}

CB_Energy::TheGUI::DoFitReturn_t CB_Energy::TheGUI::DoFit(TH1* hist, unsigned channel)
{
    if(cb_detector->IsIgnored(channel))
        return DoFitReturn_t::Skip;

    TH2* hist2 = dynamic_cast<TH2*>(hist);

    h_projection = hist2->ProjectionX("",channel,channel+1);

    func->SetDefaults(h_projection);
    const auto it_fit_param = fitParameters.find(channel);
    if(it_fit_param != fitParameters.end()) {
        VLOG(5) << "Loading previous fit parameters for channel " << channel;
        func->Load(it_fit_param->second);
    }

    func->Fit(h_projection);

    if(channel==0) {
        return DoFitReturn_t::Display;
    }

    // do not show something, goto next channel
    return DoFitReturn_t::Next;
}

void CB_Energy::TheGUI::DisplayFit()
{
    c_fit->Show(h_projection, func.get());
}

void CB_Energy::TheGUI::StoreFit(unsigned channel)
{
    const double oldValue = previousValues[channel];
    /// \todo obtain convergenceFactor and pi0mass from config or database
    const double convergenceFactor = 1.0;
    const double pi0mass = 135.0;
    const double pi0peak = func->GetPeakPosition();

    // apply convergenceFactor only to the desired procentual change of oldValue,
    // given by (pi0mass/pi0peak - 1)
    const double newValue = oldValue + oldValue * convergenceFactor * (pi0mass/pi0peak - 1);

    calibType.Values[channel] = newValue;

    const double relative_change = 100*(newValue/oldValue-1);

    LOG(INFO) << "Stored Ch=" << channel << ": PeakPosition " << pi0peak
              << " MeV,  gain changed " << oldValue << " -> " << newValue
              << " (" << relative_change << " %)";


    // don't forget the fit parameters
    fitParameters[channel] = func->Save();

    h_peaks->Fill(channel, pi0peak);
    h_relative->Fill(channel, relative_change);

    c_fit->Clear();
    c_fit->Update();
}

bool CB_Energy::TheGUI::FinishRange()
{
    c_overview->Divide(2,2);

    c_overview->cd(1);
    h_peaks->SetStats(false);
    h_peaks->Draw("P");
    c_overview->cd(2);
    TH2CB* h_peaks_cb = new TH2CB("h_peaks_cb",h_peaks->GetTitle());
    h_peaks_cb->FillElements(*h_peaks);
    h_peaks_cb->Draw("colz");

    c_overview->cd(3);
    h_relative->SetStats(false);
    h_relative->Draw("P");
    c_overview->cd(4);
    TH2CB* h_relative_cb = new TH2CB("h_relative_cb",h_relative->GetTitle());
    h_relative_cb->FillElements(*h_relative);
    h_relative_cb->Draw("colz");

    c_overview->Update();

    return true;
}

void CB_Energy::TheGUI::StoreFinishRange(const interval<TID>& range)
{
    c_overview->Clear();
    c_overview->Update();
    GUI_CalibType::StoreFinishRange(range);
}
