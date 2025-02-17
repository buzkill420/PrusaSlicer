#ifndef FILLBEDJOB_HPP
#define FILLBEDJOB_HPP

#include "ArrangeJob.hpp"

namespace Slic3r { namespace GUI {

class Plater;
class NotificationManager;

class FillBedJob : public PlaterJob
{
    int     m_object_idx = -1;

    using ArrangePolygon  = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;

    ArrangePolygons m_selected;
    ArrangePolygons m_unselected;

    Points m_bedpts;

    int m_status_range = 0;

protected:

    void prepare() override;
    void process() override;

public:
    FillBedJob(std::shared_ptr<NotificationManager> nm, Plater *plater)
        : PlaterJob{nm, plater}
    {}

    int status_range() const override
    {
        return m_status_range;
    }

    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif // FILLBEDJOB_HPP
