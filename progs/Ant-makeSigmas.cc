

#include "analysis/utils/PullsWriter.h"
#include "analysis/plot/HistogramFactories.h"

#include "base/std_ext/system.h"
#include "base/Logger.h"
#include "base/CmdLine.h"
#include "base/WrapTFile.h"
#include "base/ProgressCounter.h"
#include "base/std_ext/string.h"
#include "base/Array2D.h"

#include "analysis/plot/root_draw.h"
#include "base/BinSettings.h"
#include "base/TH_ext.h"

#include "TTree.h"
#include "TRint.h"
#include "TH3D.h"
#include "TH2D.h"
#include "TF1.h"
#include "TFitResult.h"
#include "TCanvas.h"

using namespace ant;
using namespace std;
using namespace ant::analysis;
using namespace ant::std_ext;
static volatile bool interrupt = false;


bool IsBinValid(const TH2D* hist, const int x, const int y) {
    return (x>0 && x <=hist->GetNbinsX()) && (y>0 && y <=hist->GetNbinsY());
}

const std::vector<std::pair<int,int>> neighbors = {{+1,0},{-1,0},{0,+1},{0,-1}};

double getNeighborAverage(const TH2D* hist, const int x, const int y) {

    double sum = {};
    unsigned n = 0;

    for(const auto& d : neighbors) {
        const auto bx = x + d.first;
        const auto by = y + d.second;

        if(IsBinValid(hist,bx,by)) {
            const auto v = hist->GetBinContent(bx,by);
            if(std::isfinite(v)) {
                sum += v;
                ++n;
            }
        }
    }

    return n>0 ? sum / n : 0.0;
}

unsigned getNeighborCount(const TH2D* hist, const int x, const int y) {

    unsigned n = 0;

    for(const auto& d : neighbors) {
        const auto bx = x + d.first;
        const auto by = y + d.second;

        const auto valid = IsBinValid(hist,bx,by);
        if( valid && isfinite(hist->GetBinContent(bx,by))) {
            ++n;
        }
    }

    return n;
}

void fillNeighborAverages(TH2D* hist) {


    unsigned neighbors=0;

    do {
        neighbors=0;
        int p_x =0;
        int p_y =0;

        for(int x=1; x<=hist->GetNbinsX(); ++x) {
            for(int y=1; y<=hist->GetNbinsY(); ++y) {

                if(std::isnan(hist->GetBinContent(x,y))) {
                    const auto n = getNeighborCount(hist, x,y);
                    if(n>neighbors) {
                        neighbors = n;
                        p_x = x;
                        p_y = y;
                    }
                }
            }
        }

        // if updatable bin found
        if(neighbors > 0) {
            const auto a = getNeighborAverage(hist,p_x,p_y);
            hist->SetBinContent(p_x,p_y, a);
        }
    } while(neighbors != 0);
}


/**
 * @brief projectZ
 *        code after TH3::FitSlicesZ()
 * @param hist
 * @param x
 * @param y
 * @param hf
 * @return
 *
 */

TH1D* projectZ(const TH3D* hist, const int x, const int y, HistogramFactory& hf) {

    const string name = formatter() << hist->GetName() << "_z_" << x << "_" << y;

    const auto axis = hist->GetZaxis();
    const auto bins = TH_ext::getBins(axis);

    auto h = hf.makeTH1D(name.c_str(), hist->GetZaxis()->GetTitle(), "", bins, name.c_str());
    h->Reset();

    for(int z = 0; z < int(bins.Bins()); ++z) {
        const auto v = hist->GetBinContent(x, y, z);
        if(v != .0) {
            h->Fill(axis->GetBinCenter(z), v);
            h->SetBinError(z, hist->GetBinError(x,y,z));
        }
    }

    return h;
}

vec2 maximum(const TH1D* hist) {
    return vec2( hist->GetBinCenter(hist->GetMaximumBin()), hist->GetMaximum() );
}

void ClearHistogram(TH2D* hist, const double v=0.0) {
    for(int x=1; x<=hist->GetNbinsX(); ++x) {
        for(int y=1; y<=hist->GetNbinsY(); ++y) {
            hist->SetBinContent(x,y, v);
        }
    }
}

interval<double> getZRange(const TH2& hist) {
    return {hist.GetMinimum(), hist.GetMaximum()};
}

void MakeSameZRange(std::vector<TH2*> hists) {

    if(hists.empty())
        return;


    auto b1 = getZRange(*hists.front());

    for(auto it=next(hists.begin()); it!=hists.end(); ++it) {
        b1.Extend(getZRange(**it));
    }

    for(auto h : hists) {
        h->SetMinimum(b1.Start());
        h->SetMaximum(b1.Stop());
    }

}

struct FitSlices1DHists {
    TH2D* Mean    = nullptr;
    TH2D* RMS     = nullptr;
    TH2D* Entries = nullptr;
};

FitSlices1DHists FitSlicesZ(const TH3D* hist,
                            const HistogramFactory& hf_,
                            const string& title="",
                            const double integral_cut=1000.0,
                            bool show_plots = false) {

    HistogramFactory hf(formatter() << hist->GetName() << "_FitZ", hf_);

    const auto xbins = TH_ext::getBins(hist->GetXaxis());
    const auto ybins = TH_ext::getBins(hist->GetYaxis());

    FitSlices1DHists result;

    result.RMS  = hf.makeTH2D(
                       formatter() << hist->GetTitle() << ": RMS",
                       hist->GetXaxis()->GetTitle(),
                       hist->GetYaxis()->GetTitle(),
                       xbins,
                       ybins,
                       formatter() << hist->GetName() << "_z_RMS" );
    ClearHistogram(result.RMS, std_ext::NaN);
    result.RMS->SetStats(false);

    result.Mean  = hf.makeTH2D(
                       formatter() << hist->GetTitle() << ": Mean",
                       hist->GetXaxis()->GetTitle(),
                       hist->GetYaxis()->GetTitle(),
                       xbins,
                       ybins,
                       formatter() << hist->GetName() << "_z_Mean" );
    ClearHistogram(result.Mean, std_ext::NaN);
    result.Mean->SetStats(false);

    result.Entries  = hf.makeTH2D(
                       formatter() << hist->GetTitle() << ": Entries",
                       hist->GetXaxis()->GetTitle(),
                       hist->GetYaxis()->GetTitle(),
                       xbins,
                       ybins,
                       formatter() << hist->GetName() << "_z_Entries" );
    result.Entries->SetStats(false);

    ant::canvas c(formatter() << title << ": " << hist->GetTitle() << " Fits");

    for(int y=int(ybins.Bins())-1; y >=0 ; --y) {
        for(int x=0; x < int(xbins.Bins()); ++x) {

            auto slice = projectZ(hist, x+1, y+1, hf);

            const auto integral = slice->Integral();
            result.Entries->SetBinContent(x+1,y+1, integral);

            if(integral > integral_cut) {
                const auto rms = slice->GetRMS();
                const auto mean = slice->GetMean();

                result.RMS->SetBinContent(x+1,y+1, rms);
                result.Mean->SetBinContent(x+1,y+1, mean);

            } else {
                c << padoption::SetFillColor(kGray);
            }

            c << slice;
        }
        c << endr;
    }

    if(show_plots)
        c << endc;

    return result;
}

struct Result_t {
    TH2D* pulls     = nullptr;
    TH2D* newSigmas = nullptr;
    TH2D* oldSigmas = nullptr;
    TH2D* newValues = nullptr;
    TH2D* oldValues = nullptr;
};

struct IQR_values_t : vector<vector<std_ext::IQR>> {
    BinSettings Bins_CosTheta;
    BinSettings Bins_E;
    IQR_values_t(const BinSettings& bins_CosTheta,
                 const BinSettings& bins_E) :
        vector<vector<std_ext::IQR>>(bins_CosTheta.Bins(),
                                     vector<std_ext::IQR>(bins_E.Bins())),
        Bins_CosTheta(bins_CosTheta),
        Bins_E(bins_E)
    {}

    void Fill(double cosTheta, double E, double x) {
        const auto i = Bins_CosTheta.getBin(cosTheta);
        if(i<0)
            return;
        const auto j = Bins_E.getBin(E);
        if(j<0)
            return;
        this->at(i).at(j).Add(x);
    }
};

struct Binned_TH1D_t : vector<vector<TH1D*>> {
    BinSettings Bins_CosTheta;
    BinSettings Bins_E;
    Binned_TH1D_t(const BinSettings& bins_CosTheta,
                  const BinSettings& bins_E) :
        Bins_CosTheta(bins_CosTheta),
        Bins_E(bins_E)
    {}

    void Fill(double cosTheta, double E, double x, double w) {
        const auto i = Bins_CosTheta.getBin(cosTheta);
        if(i<0)
            return;
        const auto j = Bins_E.getBin(E);
        if(j<0)
            return;
        this->at(i).at(j)->Fill(x, w);
    }
};

Result_t makeResult(const TH3D* pulls, const TH3D* sigmas, const Binned_TH1D_t& values,
                    const HistogramFactory& hf, const string& label,
                    const string& treename, const double integral_cut, const bool show_plots) {
    const string newTitle = formatter() << "new " << sigmas->GetTitle();

    auto pull_values  = FitSlicesZ(pulls,  hf, treename, integral_cut, show_plots);
    auto sigma_values = FitSlicesZ(sigmas, hf, treename, integral_cut, show_plots);

    Result_t result;

    result.oldSigmas = sigma_values.Mean;
    result.pulls     = pull_values.RMS;

    // generate oldValues from given values
    result.oldValues = hf.makeTH2D(
                           label+": Old values: Mean",
                           "cos #theta","Ek",
                           values.Bins_CosTheta, values.Bins_E,
                           "values_old_"+label);
    ClearHistogram(result.oldValues, std_ext::NaN);
    for(auto x=0u;x<values.size();x++) {
        for(auto y=0u;y<values.at(x).size();y++) {
            auto h = values.at(x).at(y);
            if(h->Integral() > integral_cut)
                result.oldValues->SetBinContent(x+1,y+1,h->GetMean());
        }
    }
    // flood fill oldValues
    {
        auto wrapper = Array2D_TH2D(result.oldValues);
        FloodFillAverages::fillNeighborAverages(wrapper);
    }


    // calculate new Sigmas by new_sigma = old_sigma * pull_RMS
    result.newSigmas = hf.clone(result.oldSigmas, "sigma_"+label);
    result.newSigmas->Multiply(result.pulls);

    // flood fill newSigmas
    {
        auto wrapper = Array2D_TH2D(result.newSigmas);
        FloodFillAverages::fillNeighborAverages(wrapper);
    }
    result.newSigmas->SetTitle(newTitle.c_str());

    MakeSameZRange( {result.newSigmas,result.oldSigmas} );

    // generate newValues by newValue = old_value + pull_mean * sigma_mean
    result.newValues = hf.clone(result.oldValues, "new_values_"+label);

    return result;
}



int main( int argc, char** argv )
{
    SetupLogger();

    signal(SIGINT, [] (int) {
        LOG(INFO) << ">>> Interrupted";
        interrupt = true;
    });

    TCLAP::CmdLine cmd("Ant-makeSigmas", ' ', "0.1");

    auto cmd_input        = cmd.add<TCLAP::ValueArg<string>>("i","input",      "pull trees",                                     true,  "", "rootfile");
    auto cmd_output       = cmd.add<TCLAP::ValueArg<string>>("o","",           "sigma hists",                                    false, "", "rootfile");
    auto cmd_batchmode    = cmd.add<TCLAP::MultiSwitchArg>  ("b","batch",      "Run in batch mode (no ROOT shell afterwards)",   false);
    auto cmd_maxevents    = cmd.add<TCLAP::MultiArg<int>>   ("m","maxevents",  "Process only max events",                        false, "maxevents");
    auto cmd_tree         = cmd.add<TCLAP::ValueArg<string>>("t","tree",       "Treename",false,"InterpolatedPulls/pulls_photon_cb","treename");
    auto cmd_fitprob_cut  = cmd.add<TCLAP::ValueArg<double>>("", "fitprob_cut" ,"Min. required Fit Probability",                 false, 0.01,"probability");
    auto cmd_integral_cut = cmd.add<TCLAP::ValueArg<double>>("", "integral_cut","Min. required integral in Bins",                false, 100.0,"integral");
    auto cmd_show_plots   = cmd.add<TCLAP::MultiSwitchArg>  ("", "show_plots"  ,"Show detail plots for each parameter",          false);

    cmd.parse(argc, argv);

    const auto fitprob_cut  = cmd_fitprob_cut->getValue();
    const auto integral_cut = cmd_integral_cut->getValue();

    WrapTFileInput input(cmd_input->getValue());

    TTree* tree;
    if(!input.GetObject(cmd_tree->getValue(), tree)) {
        LOG(ERROR) << "Cannot find tree " << cmd_tree->getValue() << " in " << cmd_input->getValue();
        exit(EXIT_FAILURE);
    }

    utils::PullsWriter::PullTree_t pulltree;
    if(!pulltree.Matches(tree)) {
        LOG(ERROR) << "Given tree is not a PullTree_t";
        exit(EXIT_FAILURE);
    }
    pulltree.LinkBranches(tree);
    auto entries = pulltree.Tree->GetEntries();

    unique_ptr<WrapTFileOutput> masterFile;
    if(cmd_output->isSet()) {
        masterFile = std_ext::make_unique<WrapTFileOutput>(cmd_output->getValue(),
                                                    WrapTFileOutput::mode_t::recreate,
                                                     true); // cd into masterFile upon creation
    }

    struct hist_settings_t {
        string name;
        vector<string> labels;
        BinSettings bins_cosTheta{0};
        BinSettings bins_E{0};
        vector<BinSettings> bins_Sigmas;
    };

    auto get_hist_settings = [] (const std::string& treename) {
        hist_settings_t s;
        // do some string parsing to figure out what
        // particletype (proton/photon) and detector (CB/TAPS) we're dealing with
        const auto& parts = std_ext::tokenize_string(std_ext::basename(treename),"_");
        const auto& particletype = parts.at(1);
        const auto& detectortype = parts.at(2);

        s.name = std_ext::formatter() << "sigma_" << particletype << "_" << detectortype;
        if(particletype == "photon") {
            s.bins_E = {10, 0, 1000};
        }
        else if(particletype == "proton") {
            /// \todo support measured proton (use more than one energy bin)
            s.bins_E = {1, 0, 1000};
        }

        if(detectortype == "cb") {
            s.bins_cosTheta = {15, std::cos(std_ext::degree_to_radian(160.0)), std::cos(std_ext::degree_to_radian(20.0))};
            s.labels = {"Ek","Theta","Phi","R"};
            s.bins_Sigmas = {
                {50,0.0,50.0}, // Ek
                {50,0.0,degree_to_radian(10.0)}, // Theta
                {50,0.0,degree_to_radian(10.0)}, // Phi
                {50,0.0,5.0}   // R
            };
        }
        else if(detectortype == "taps") {
            s.bins_cosTheta = {10, std::cos(std_ext::degree_to_radian(25.0)), std::cos(std_ext::degree_to_radian(0.0))};
            s.labels = {"Ek","Rxy","Phi","L"};
            s.bins_Sigmas = {
                {50,0.0,50.0},  // Ek
                {50,0.0,5.0},   // Rxy
                {50,0.0,degree_to_radian(10.0)}, // Phi
                {50,0.0,5.0}    // L
            };
        }

        return s;
    };

    const hist_settings_t& hist_settings = get_hist_settings(cmd_tree->getValue());
    if(hist_settings.labels.empty()) {
        LOG(ERROR) << "Could not identify pulltree name";
        exit(EXIT_FAILURE);
    }
    HistogramFactory HistFac(hist_settings.name);

    /// \todo remove hardcoded number of parameters,
    /// use stuff from utils/Fitter.h instead?
    constexpr auto nParameters = 4;


    // create 3D hists for pulls/sigmas
    std::vector<TH3D*> h_pulls;
    std::vector<TH3D*> h_sigmas;

    for(auto n=0;n<nParameters;n++) {
        auto& label = hist_settings.labels.at(n);
        h_pulls.push_back(HistFac.makeTH3D(
                              "Pulls "+label,"cos #theta","Ek","Pulls "+label,
                              hist_settings.bins_cosTheta,
                              hist_settings.bins_E,
                              {50, -5, 5}, // BinSettings identical for all pulls
                              "h_pulls_"+label
                              ));

        h_sigmas.push_back(HistFac.makeTH3D(
                               "Sigmas "+label,"cos #theta","Ek","Sigmas "+label,
                               hist_settings.bins_cosTheta,
                               hist_settings.bins_E,
                               hist_settings.bins_Sigmas.at(n),
                               "h_sigmas_"+label
                               ));
    }

    // create RMS_t for values (instead of TH3D),
    // as binning varies wildly for
    // different parameters in CB/TAPS
    vector<IQR_values_t> IQR_values(nParameters,
                                    IQR_values_t{
                                        hist_settings.bins_cosTheta,
                                        hist_settings.bins_E
                                    });

    LOG(INFO) << "Tree entries=" << entries;
    LOG(INFO) << "Cut probability > " << fitprob_cut;

    auto firstentries = (decltype(entries))min( 100*IQR_values.size()*IQR_values.at(0).size(), (size_t)std::round(0.1*entries) );
    if(firstentries<entries){
        LOG(WARNING) << "Probably not enough entries to produce meaningful result";
        firstentries = entries;
    }

    LOG(INFO) << "Processing first part until entry=" << firstentries;

    long long entry = 0;
    ProgressCounter::Interval = 3;
    ProgressCounter progress(
                [&entry, entries] (std::chrono::duration<double>) {
        LOG(INFO) << "Processed " << 100.0*entry/entries << " %";
    });

    for(entry=0;entry<firstentries;entry++) {
        if(interrupt)
            break;

        progress.Tick();
        pulltree.Tree->GetEntry(entry);

        if(pulltree.FitProb > fitprob_cut ) {
            for(auto n=0u;n<pulltree.Values().size();n++) {
                IQR_values.at(n).Fill(cos(pulltree.Theta), pulltree.E, pulltree.Values().at(n));
            }
        }
    }

    // now create h_values histograms with different binning for each (costheta, E)

    vector<Binned_TH1D_t> h_values(nParameters, Binned_TH1D_t{
                                       hist_settings.bins_cosTheta,
                                       hist_settings.bins_E
                                   });
    for(auto n=0;n<nParameters;n++) {
        auto& label = hist_settings.labels.at(n);
        HistogramFactory HistFacValues("h_values_"+label, HistFac);
        const auto& IQR_value = IQR_values[n];
        auto& h_value = h_values[n];
        for(auto i=0u;i<IQR_value.size();i++) {
            h_value.emplace_back();
            auto& h_values_i = h_value.back();
            for(auto j=0u;j<IQR_value[i].size();j++) {
                const auto& iqr = IQR_value[i][j];
                /// \todo tune parameters cut and width factor?
                const auto center = iqr.GetN() > 10 ?   iqr.GetMedian()    : 0;
                const auto width  = iqr.GetN() > 10 ? 8*iqr.GetIQRStdDev() : 1;
                BinSettings bins{50, interval<double>::CenterWidth(center, width)};
                h_values_i.emplace_back(
                            HistFacValues.makeTH1D("Values",label,"#",bins,
                                                   to_string(i)+"_"+to_string(j)));
            }
        }
    }


    auto max_entries = entries;
    if(cmd_maxevents->isSet() && cmd_maxevents->getValue().back()<entries) {
        max_entries = cmd_maxevents->getValue().back();
        LOG(INFO) << "Running until " << max_entries;
    }

    for(entry=0;entry<max_entries;entry++) {
        if(interrupt)
            break;

        progress.Tick();
        pulltree.Tree->GetEntry(entry);

        if(pulltree.FitProb > fitprob_cut ) {

            for(auto n=0u;n<pulltree.Pulls().size();n++) {
                h_pulls.at(n)->Fill(cos(pulltree.Theta), pulltree.E,
                                    pulltree.Pulls().at(n), pulltree.TaggW);
            }

            for(auto n=0u;n<pulltree.Sigmas().size();n++) {
                h_sigmas.at(n)->Fill(cos(pulltree.Theta), pulltree.E,
                                     pulltree.Sigmas().at(n), pulltree.TaggW);
            }

            for(auto n=0u;n<pulltree.Sigmas().size();n++) {
                h_values.at(n).Fill(cos(pulltree.Theta), pulltree.E,
                                    pulltree.Values().at(n), pulltree.TaggW);
            }
        }
    }

    argc=1; // prevent TRint to parse any cmdline except prog name
    auto app = cmd_batchmode->isSet() || !std_ext::system::isInteractive()
               ? nullptr
               : std_ext::make_unique<TRint>("Ant-makeSigmas",&argc,argv,nullptr,0,true);

    std::vector<Result_t> results;
    for(auto n=0;n<nParameters;n++) {
        auto& label = hist_settings.labels.at(n);
        auto r = makeResult(h_pulls.at(n),
                            h_sigmas.at(n),
                            h_values.at(n),
                            HistFac,
                            label,
                            cmd_tree->getValue(),
                            integral_cut, cmd_show_plots->isSet());
        results.emplace_back(r);

    }

    if(app) {
        canvas summary(cmd_tree->getValue());
        summary << drawoption("colz");

        for( auto r : results) {
            summary << r.newSigmas << r.oldSigmas << r.pulls << endr;
        }
        summary << endc;

        if(masterFile)
            LOG(INFO) << "Stopped running, but close ROOT properly to write data to disk.";

        app->Run(kTRUE); // really important to return...
        if(masterFile)
            LOG(INFO) << "Writing output file...";
        masterFile = nullptr;   // and to destroy the master WrapTFile before TRint is destroyed
    }

}
