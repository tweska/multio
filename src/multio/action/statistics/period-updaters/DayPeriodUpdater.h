
#pragma once

#include <iostream>
#include <iomanip> // For setw

#include "multio/action/statistics/cfg/StatisticsConfiguration.h"
#include "multio/action/statistics/period-updaters/PeriodUpdater.h"
#include "multio/action/statistics/TimeUtils.h"
#include "eckit/types/DateTime.h"
#include "multio/message/Message.h"


namespace multio::action {


class DayPeriodUpdater final : public PeriodUpdater {
public:

    DayPeriodUpdater(long span) : PeriodUpdater{span} {};

    DayPeriodUpdater(std::shared_ptr<StatisticsIO>& IOmanager, const StatisticsOptions& opt):
        PeriodUpdater{  timeUnit(), IOmanager, opt}{ };

    void dump( std::shared_ptr<StatisticsIO>& IOmanager,  const StatisticsOptions& opt ) const {
       std::ostringstream os;
       os  <<  timeUnit() << "_" << std::setw(4) << std::setfill('0') << span_;
       PeriodUpdater::baseDump( timeUnit(), IOmanager, opt);
    };


    const std::string name() const {
        std::ostringstream os;
        os << std::setw(4) << std::setfill('0') << span_ << "-"
           << timeUnit();
        return os.str();
    };

    const std::string timeUnit() const {
        std::ostringstream os;
        os << "day";
        return os.str();
    };

    eckit::DateTime computeWinStartTime(const eckit::DateTime& nextTime) const {
        const auto& d = nextTime.date();
        const auto& t = nextTime.time();
        return eckit::DateTime{d, eckit::Time{0}};
    };

    eckit::DateTime updateWinEndTime(const eckit::DateTime& startPoint) const {
        eckit::DateTime tmp = startPoint + static_cast<eckit::Second>(86400 * span_);
        return eckit::DateTime{tmp.date(), eckit::Time{0}};
    };

};


}
