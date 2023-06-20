#pragma once

namespace erhe::toolkit
{

class Context_window;

void initialize_frame_capture();
void start_frame_capture     (const Context_window& context_window);
void end_frame_capture       (const Context_window& context_window);

} // namespace erhe::toolkit
