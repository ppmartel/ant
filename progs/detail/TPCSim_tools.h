#pragma once
#include <vector>
#include <list>
#include "base/vec/vec2.h"
#include <ostream>
#include "base/math_functions/Linear.h"
#include "base/interval.h"
#include "base/std_ext/math.h"
#include "base/WrapTTree.h"


#include "APLCON.hpp"

class TGraphErrors;
class TGraph;
class TF1;
class TH2D;


namespace TPCSim {

using track_t = ant::math::LineFct;
struct Value_t {

    constexpr Value_t(double v, double s) : V_S_P{v, s, 0}{}

    std::tuple<double,double,double> V_S_P; // short for Value, Sigma, Pull

    bool Fixed = false;
    bool Poisson = false;

    // fitter is able to access fields to fit them (last one pull is just written, actually)
    template<size_t N>
    std::tuple<double&> linkFitter() noexcept {
        return std::tie(std::get<N>(V_S_P));
    }

    // user can provide settings for each variable
    template<size_t innerIdx>
    APLCON::Variable_Settings_t getFitterSettings(size_t outerIdx) const noexcept {
        (void)outerIdx; // unused, provided to user method for completeness
        APLCON::Variable_Settings_t settings;
        if(Fixed)
            settings.StepSize = 0;
        if(Poisson)
            settings.Distribution = APLCON::Distribution_t::Poissonian;
        return settings;
    }

    // the following methods are just user convenience (not required by fitter)

    friend std::ostream& operator<<(std::ostream& s, const Value_t& o) {
        return s << "(" << o.Value() << "," << o.Sigma() << "," << o.Pull() << ")";
    }

    double Value() const {
        return std::get<APLCON::ValueIdx>(V_S_P);
    }

    double Sigma() const {
        return std::get<APLCON::SigmaIdx>(V_S_P);
    }

    double Pull() const {
        return std::get<APLCON::PullIdx>(V_S_P);
    }

    operator double() const {
        return std::get<APLCON::ValueIdx>(V_S_P);
    }
};


struct diffusion_t {

    double D; //um/sqrt(cm)

    diffusion_t(const double d):
        D(d)
    {}

    double widthAt(const double z) const {
        return D*sqrt(z);
    }

};

struct driftproperties {
    diffusion_t transverse;
    diffusion_t longitudinal;
    driftproperties(const double dt, const double dl):
        transverse(dt),
        longitudinal(dl)
    {}

};

struct resolution_t {
    double dt;
    double dl;
    resolution_t(const double rtrans, const double rlong):
        dt(rtrans),
        dl(rlong)
    {}
};

struct tpcproperties {
    static constexpr const double CBradius = 24.5; //cm
    double rin    =  7.0;
    double rout   = 14.0;

    static ant::interval<double> tpcInCB(const double l, const double rout);
    ant::interval<double> length;
    int nRings    = 10;

    double ringWidth() const {
         return (rout - rin) / nRings;
    }

    TGraph* getOutline() const;

    tpcproperties(const double len=35.0);
};

/**
 * @brief generate Points along a track, in the r-z plane
 * @param z0
 * @param theta
 * @param prop
 * @return
 */
std::vector<ant::vec2> generatePoints(const double z0, const double theta,
                                 const resolution_t& prop, const tpcproperties& tpc);

ant::vec2 generatePoint(const double r, const track_t& track, const resolution_t& prop);



struct trackFitter_t
{


    APLCON::Fitter<Value_t, Value_t,std::vector<Value_t>,std::vector<Value_t>> fitter;

    trackFitter_t() = default;

    struct result_t
    {
        Value_t Intercept{0,0};   // unmeasured
        Value_t Slope{0,0};       // unmeasured

        std::vector<Value_t> Fitted_Rs;
        std::vector<Value_t> Fitted_Zs;
        APLCON::Result_t FitStats;

        double GetDZ0(const double trueZ0) const noexcept { return trueZ0 - Intercept; }
        double GetDTheta(const double trueTheta) const noexcept {return M_PI_2 - sin(Slope) - trueTheta;}
        double GetDVertex(const ant::vec2& trueVertex = {0,0}) const noexcept {return fabs(Intercept + Slope * trueVertex.x - trueVertex.y) / sqrt(1+Slope*Slope);}
    };
    trackFitter_t::result_t DoFit(const std::vector<ant::vec2>& points, const resolution_t& res, const tpcproperties& tpc);


};

ant::vec2 getUncertainties(const ant::vec2&, const resolution_t& res, const tpcproperties& tpc);

struct draw
{
    static TH2D* makeCanvas(const tpcproperties&);
    static TGraphErrors* makeGraph(const std::vector<ant::vec2>& points, const resolution_t& res, const tpcproperties& tpc);
    static TF1* makeFitTF1(const trackFitter_t::result_t& tFitter);
    static std::list<TGraph*> makeScene(const tpcproperties& tpc);
};

struct tree_t: ant::WrapTTree
{
    ADD_BRANCH_T(int, nHitsCut)
    ADD_BRANCH_T(int, nHits)

    ADD_BRANCH_T(double, genZ0)
    ADD_BRANCH_T(double, genTheta)

    ADD_BRANCH_T(double, dZ0)
    ADD_BRANCH_T(double, dTheta)

    static constexpr const char* treeName() { return "tree"; }
};

}
