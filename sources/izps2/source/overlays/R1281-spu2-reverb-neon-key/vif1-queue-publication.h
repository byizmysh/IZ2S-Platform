// SPDX-License-Identifier: GPL-3.0+
#pragma once

void iz_ps2_set_vif1_batch_publication(bool enabled);
bool iz_ps2_begin_vif1_batch_publication();
void iz_ps2_end_vif1_batch_publication();

class Vif1QueuePublicationScope
{
public:
    Vif1QueuePublicationScope() : m_owns_batch(iz_ps2_begin_vif1_batch_publication()) {}
    ~Vif1QueuePublicationScope()
    {
        if (m_owns_batch)
            iz_ps2_end_vif1_batch_publication();
    }

    Vif1QueuePublicationScope(const Vif1QueuePublicationScope&) = delete;
    Vif1QueuePublicationScope& operator=(const Vif1QueuePublicationScope&) = delete;

private:
    const bool m_owns_batch;
};
