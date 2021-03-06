//----------------------------------------------------------------------------
//
//  This file is part of seq42.
//
//  seq42 is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General)mm Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  seq42 is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with seq42; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//-----------------------------------------------------------------------------
#include "event.h"
#include "perfroll.h"
#include "seqedit.h"
#include "font.h"


perfroll::perfroll( perform *a_perf,
                    mainwnd *a_main,
                    Adjustment * a_hadjust,
                    Adjustment * a_vadjust  ) :
    m_window(NULL),
    m_black(Gdk::Color("black")),
    m_white(Gdk::Color("white")),
    m_blue(Gdk::Color("SteelBlue1")),
    m_grey(Gdk::Color("grey")),
    m_lt_grey(Gdk::Color("light grey")),
    m_pixmap(NULL),
    m_mainperf(a_perf),
    m_mainwnd(a_main),

    m_perf_scale_x(c_perf_scale_x),       // 32 ticks per pixel
    m_zoom(c_perf_scale_x),               // 32 ticks per pixel

    m_old_progress_ticks(0),

    m_4bar_offset(0),
    m_track_offset(0),
    m_roll_length_ticks(0),
    m_drop_y(0),
    m_drop_track(0),

    m_vadjust(a_vadjust),
    m_hadjust(a_hadjust),

    m_moving(false),
    m_growing(false),

    cross_track_paste(false),
    have_button_press(false),
    transport_follow(true),
    trans_button_press(false)
{
    Glib::RefPtr<Gdk::Colormap> colormap = get_default_colormap();
    colormap->alloc_color( m_black );
    colormap->alloc_color( m_white );
    colormap->alloc_color( m_blue );
    colormap->alloc_color( m_grey );
    colormap->alloc_color( m_lt_grey );

    //m_text_font_6_12 = Gdk_Font( c_font_6_12 );

    add_events( Gdk::BUTTON_PRESS_MASK |
                Gdk::BUTTON_RELEASE_MASK |
                Gdk::POINTER_MOTION_MASK |
                Gdk::KEY_PRESS_MASK |
                Gdk::KEY_RELEASE_MASK |
                Gdk::FOCUS_CHANGE_MASK |
                Gdk::SCROLL_MASK );

    set_size_request( 10, 10 );

    set_double_buffered( false );

    for( int i=0; i<c_max_track; ++i )
        m_track_active[i]=false;
}

perfroll::~perfroll( )
{

}

void
perfroll::change_horz( )
{
    if ( m_4bar_offset != (int) m_hadjust->get_value() )
    {
        m_4bar_offset = (int) m_hadjust->get_value();
        queue_draw();
    }
}

void
perfroll::change_vert( )
{
    if ( m_track_offset != (int) m_vadjust->get_value() )
    {
        /*   must adjust m_drop_y or perfroll_input unselect_triggers will not work if   */
        /*   scrolled up or down to a new location - see note in on_button_press_event() */
        /*   in perfroll_input.cpp */

        m_drop_y += ((m_track_offset - (int) m_vadjust->get_value()) *c_names_y);

        m_track_offset = (int) m_vadjust->get_value();
        queue_draw();
    }
}

void
perfroll::on_realize()
{
    // we need to do the default realize
    Gtk::DrawingArea::on_realize();

    set_flags( Gtk::CAN_FOCUS );

    // Now we can allocate any additional resources we need
    m_window = get_window();
    m_gc = Gdk::GC::create( m_window );
    m_window->clear();

    update_sizes();

    m_hadjust->signal_value_changed().connect( mem_fun( *this, &perfroll::change_horz ));
    m_vadjust->signal_value_changed().connect( mem_fun( *this, &perfroll::change_vert ));

    /*
        This creation of m_background needs to be set to the max width for proper drawing of zoomed
        measures or they will get truncated with high beats per measure and low beat width. Since this
        is a constant size, it cannot be adjusted later for zoom. The constant c_perfroll_background_x
        is set to the max amount by default for use here. The drawing functions fill_background_pixmap()
        and draw_background_on() which use c_perfroll_background_x also, could be adjusted by zoom with
        a substituted variable. Not sure if there is any benefit to doing the adjustment...
        Perhaps a small benefit in speed? Maybe FIXME if really, really bored...
    */

    m_background = Gdk::Pixmap::create( m_window,
                                        c_perfroll_background_x,
                                        c_names_y, -1 );

    /* and fill the background ( dotted lines n' such ) */
    fill_background_pixmap();
}

void
perfroll::init_before_show( )
{
    m_roll_length_ticks = m_mainperf->get_max_trigger();
    m_roll_length_ticks = m_roll_length_ticks -
                          ( m_roll_length_ticks % ( c_ppqn * 16 ));
    m_roll_length_ticks +=  c_ppqn * 4096;
}

void
perfroll::update_sizes()
{
    int h_bars         = m_roll_length_ticks / (c_ppqn * 16);
    int h_bars_visable = (m_window_x * m_perf_scale_x) / (c_ppqn * 16);

    m_hadjust->set_lower( 0 );
    m_hadjust->set_upper( h_bars );
    m_hadjust->set_page_size( h_bars_visable );
    m_hadjust->set_step_increment( 1 );
    m_hadjust->set_page_increment( 1 );
    int h_max_value = h_bars - h_bars_visable;

    if ( m_hadjust->get_value() > h_max_value )
    {
        m_hadjust->set_value( h_max_value );
    }

    m_vadjust->set_lower( 0 );
    m_vadjust->set_upper( c_max_track );
    m_vadjust->set_page_size( m_window_y / c_names_y );
    m_vadjust->set_step_increment( 1 );
    m_vadjust->set_page_increment( 1 );

    int v_max_value = c_max_track - (m_window_y / c_names_y);

    if ( m_vadjust->get_value() > v_max_value )
    {
        m_vadjust->set_value(v_max_value);
    }

    if ( is_realized() )
    {
        m_pixmap = Gdk::Pixmap::create( m_window,
                                        m_window_x,
                                        m_window_y, -1 );
    }

    queue_draw();
}

void
perfroll::increment_size()
{
    m_roll_length_ticks += (c_ppqn * 512);
    update_sizes( );
}

/* updates background */
void
perfroll::fill_background_pixmap()
{
    /* clear background */
    m_gc->set_foreground(m_white);
    m_background->draw_rectangle
    (
        m_gc,true,
        0,
        0,
        c_perfroll_background_x,
        c_names_y
    );

    /* draw horz grey lines */
    m_gc->set_foreground(m_grey);

    gint8 dash = 1;
    m_gc->set_dashes( 0, &dash, 1 );

    m_gc->set_line_attributes( 1,
                               Gdk::LINE_ON_OFF_DASH,
                               Gdk::CAP_NOT_LAST,
                               Gdk::JOIN_MITER );

    m_background->draw_line
    (
        m_gc,
        0,
        0,
        c_perfroll_background_x,
        0
    );

    int beats = m_measure_length / m_beat_length;

    /* draw vert lines */
    for ( int i=0; i< beats ; )
    {
        if ( i == 0 )
        {
            m_gc->set_line_attributes( 1,
                                       Gdk::LINE_SOLID,
                                       Gdk::CAP_NOT_LAST,
                                       Gdk::JOIN_MITER );
        }
        else
        {
            m_gc->set_line_attributes( 1,
                                       Gdk::LINE_ON_OFF_DASH,
                                       Gdk::CAP_NOT_LAST,
                                       Gdk::JOIN_MITER );
        }

        m_gc->set_foreground(m_grey);

        /* solid line on every beat */
        m_background->draw_line
        (
            m_gc,
            i * m_beat_length / m_perf_scale_x,
            0,
            i * m_beat_length / m_perf_scale_x,
            c_names_y
        );

        // jump 2 if 16th notes
        if ( m_beat_length < c_ppqn/2 )
        {
            i += (c_ppqn / m_beat_length);
        }
        else
        {
            ++i;
        }
    }

    /* reset line style */

    m_gc->set_line_attributes( 1,
                               Gdk::LINE_SOLID,
                               Gdk::CAP_NOT_LAST,
                               Gdk::JOIN_MITER );
}


/* simply sets the snap member */
void
perfroll::set_guides( int a_snap, int a_measure, int a_beat )
{
    m_snap = a_snap;
    m_measure_length = a_measure;
    m_beat_length = a_beat;

    if ( is_realized() )
    {
        fill_background_pixmap();
    }

    queue_draw();
}

void
perfroll::draw_progress()
{
    long tick = m_mainperf->get_tick();
    long tick_offset = m_4bar_offset * c_ppqn * 16;

    int progress_x =     ( tick - tick_offset ) / m_perf_scale_x ;
    int old_progress_x = ( m_old_progress_ticks - tick_offset ) / m_perf_scale_x ;

    /* draw old */
    m_window->draw_drawable
    (
        m_gc,
        m_pixmap,
        old_progress_x-1, 0,
        old_progress_x-1, 0,
        2, m_window_y
    );

    m_gc->set_line_attributes( 2,
                           Gdk::LINE_SOLID,
                           Gdk::CAP_NOT_LAST,
                           Gdk::JOIN_MITER );

    m_gc->set_foreground(m_black);

    m_window->draw_line
    (
        m_gc,
        progress_x, 0,
        progress_x, m_window_y
    );
    // reset
    m_gc->set_line_attributes( 1,
                           Gdk::LINE_SOLID,
                           Gdk::CAP_NOT_LAST,
                           Gdk::JOIN_MITER );
    m_old_progress_ticks = tick;

    auto_scroll_horz();
}



void perfroll::draw_track_on( Glib::RefPtr<Gdk::Drawable> a_draw, int a_track )
{
    long tick_on;
    long tick_off;
    long offset;
    bool selected;
    int seq_idx;
    sequence *seq;

    long tick_offset = m_4bar_offset * c_ppqn * 16;
    long x_offset = tick_offset / m_perf_scale_x;

    if ( a_track < c_max_track )
    {
        if ( m_mainperf->is_active_track( a_track ))
        {
            m_track_active[a_track] = true;

            track *trk =  m_mainperf->get_track( a_track );
            trk->reset_draw_trigger_marker();
            a_track -= m_track_offset;

            while ( trk->get_next_trigger( &tick_on, &tick_off, &selected, &offset, &seq_idx ))
            {
                if ( tick_off > 0 )
                {
                    long x_on  = tick_on  / m_perf_scale_x;
                    long x_off = tick_off / m_perf_scale_x;
                    int  w     = x_off - x_on + 1;

                    int x = x_on;
                    int y = c_names_y * a_track + 1;  // + 2
                    int h = c_names_y - 2; // - 4

                    // adjust to screen corrids
                    x = x - x_offset;

                    if ( selected )
                        m_gc->set_foreground(m_blue);
                    else
                        m_gc->set_foreground(m_white);

                    /* trigger background */
                    a_draw->draw_rectangle(m_gc,true,
                                           x,
                                           y,
                                           w,
                                           h );
                    /* trigger outline */
                    m_gc->set_foreground(m_black);
                    a_draw->draw_rectangle(m_gc,false,
                                           x,
                                           y,
                                           w,
                                           h );
                    /* resize handle - top left */
                    m_gc->set_foreground(m_black);
                    a_draw->draw_rectangle(m_gc,false,
                                           x,
                                           y,
                                           c_perfroll_size_box_w,
                                           c_perfroll_size_box_w );
                    /* resize handle - bottom right */
                    a_draw->draw_rectangle(m_gc,false,
                                           x+w-c_perfroll_size_box_w,
                                           y+h-c_perfroll_size_box_w,
                                           c_perfroll_size_box_w,
                                           c_perfroll_size_box_w );

                    m_gc->set_foreground(m_black);

                    char label[40];
                    int max_label = (w / 6)-1;
                    if(max_label < 0) max_label = 0;
                    if(max_label > 39) max_label = 39;
                    seq = trk->get_sequence(seq_idx);
                    if(seq == NULL)
                    {
                        strncpy(label, "Empty", max_label);
                    }
                    else
                    {
                        strncpy(label, seq->get_name(), max_label);
                        long sequence_length = seq->get_length();
                        int length_w = sequence_length / m_perf_scale_x;
                        long length_marker_first_tick = ( tick_on - (tick_on % sequence_length) + (offset % sequence_length) - sequence_length);

                        long tick_marker = length_marker_first_tick;

                        while ( tick_marker < tick_off )
                        {
                            long tick_marker_x = (tick_marker / m_perf_scale_x) - x_offset;

                            if ( tick_marker > tick_on ) // sequence end marker
                            {
                                if ( selected )
                                    m_gc->set_foreground(m_white);
                                else
                                    m_gc->set_foreground(m_grey);

                                a_draw->draw_rectangle(m_gc,true,
                                                       tick_marker_x,
                                                       y+4,
                                                       1,
                                                       h-8 );
                            }

                            int lowest_note = seq->get_lowest_note_event( );
                            int highest_note = seq->get_highest_note_event( );

                            int height = highest_note - lowest_note;
                            height += 2;

                            int length = seq->get_length( );

                            long tick_s;
                            long tick_f;
                            int note;

                            bool selected;

                            int velocity;
                            draw_type dt;

                            seq->reset_draw_marker();

                            m_gc->set_foreground(m_black); // notes
                            while ( (dt = seq->get_next_note_event( &tick_s, &tick_f, &note,
                                                                    &selected, &velocity )) != DRAW_FIN )
                            {
                                int note_y = ((c_names_y-6) -
                                              ((c_names_y-6)  * (note - lowest_note)) / height) + 1;

                                int tick_s_x = ((tick_s * length_w)  / length) + tick_marker_x;
                                int tick_f_x = ((tick_f * length_w)  / length) + tick_marker_x;

                                if ( dt == DRAW_NOTE_ON || dt == DRAW_NOTE_OFF )
                                    tick_f_x = tick_s_x + 1;
                                if ( tick_f_x <= tick_s_x )
                                    tick_f_x = tick_s_x + 1;

                                if ( tick_s_x < x )
                                {
                                    tick_s_x = x;
                                }

                                if ( tick_f_x > x + w )
                                {
                                    tick_f_x = x + w;
                                }

                                /*
                                        [           ]
                                 -----------
                                                 ---------
                                       ----------------
                                ------                      ------
                                */

                                //printf("tick_s [%ld]: tick_f[%ld]\n", tick_s, tick_f);
                                //printf("tick_f_x [%d]: tick_s_x[%d]\n", tick_f_x, tick_s_x);

                                if ( tick_f_x >= x && tick_s_x <= x+w )
                                    m_pixmap->draw_line(m_gc, tick_s_x,
                                                        y + note_y,
                                                        tick_f_x,
                                                        y + note_y );
                            }

                            tick_marker += sequence_length;
                        }
                    }
                    label[max_label] = '\0';
                    p_font_renderer->render_string_on_drawable(m_gc,
                            x+5,
                            y+2,
                            a_draw, label, font::BLACK );
                }
            }
        }
    }
}

void perfroll::draw_background_on( Glib::RefPtr<Gdk::Drawable> a_draw, int a_track )
{
    long tick_offset = m_4bar_offset * c_ppqn * 16;
    long first_measure = tick_offset / m_measure_length;

    a_track -= m_track_offset;

    int y = c_names_y * a_track;
    int h = c_names_y;

    m_gc->set_foreground(m_white);
    a_draw->draw_rectangle(m_gc,true,
                           0,
                           y,
                           m_window_x,
                           h );

    m_gc->set_foreground(m_black);

    for ( int i = first_measure;
            i < first_measure + (m_window_x * m_perf_scale_x / (m_measure_length)) + 1; i++ )
    {
        int x_pos = ((i * m_measure_length) - tick_offset) / m_perf_scale_x;

        a_draw->draw_drawable(m_gc, m_background,
                              0,
                              0,
                              x_pos,
                              y,
                              c_perfroll_background_x,
                              c_names_y);
    }
}

bool
perfroll::on_expose_event(GdkEventExpose* e)
{
    int y_s = e->area.y / c_names_y;
    int y_f = (e->area.y  + e->area.height) / c_names_y;

    for ( int y=y_s; y<=y_f; y++ )
    {
        draw_background_on(m_pixmap, y + m_track_offset );
        draw_track_on(m_pixmap, y + m_track_offset );
    }

    m_window->draw_drawable( m_gc, m_pixmap,
                             e->area.x,
                             e->area.y,
                             e->area.x,
                             e->area.y,
                             e->area.width,
                             e->area.height );
    return true;
}


void
perfroll::redraw_dirty_tracks()
{
    bool draw = false;

    int y_s = 0;
    int y_f = m_window_y / c_names_y;

    for ( int y=y_s; y<=y_f; y++ )
    {
        int track = y + m_track_offset; // 4am

        bool dirty = (m_mainperf->is_dirty_perf(track ));

        if (dirty)
        {
            draw_background_on(m_pixmap,track );
            draw_track_on(m_pixmap,track);
            draw = true;
        }
    }

    if ( draw )
        m_window->draw_drawable
        (
            m_gc,
            m_pixmap,
            0,
            0,
            0,
            0,
            m_window_x,
            m_window_y
        );
}

void
perfroll::draw_drawable_row( Glib::RefPtr<Gdk::Drawable> a_dest, Glib::RefPtr<Gdk::Drawable> a_src,  long a_y )
{
    if( a_y < 0) // if user scrolled up off the window
        return;

    int s = a_y / c_names_y;

    a_dest->draw_drawable
    (
        m_gc,
        a_src,
        0,
        c_names_y * s,
        0,
        c_names_y * s,
        m_window_x,
        c_names_y
    );
}

bool
perfroll::on_button_press_event(GdkEventButton* a_ev)
{
    if(!trans_button_press) // to avoid double button press on normal seq42 method
    {
        transport_follow = m_mainperf->get_follow_transport();
        m_mainperf->set_follow_transport(false);
        trans_button_press = true;
    }

    bool result;

    switch (global_interactionmethod)
    {
    case e_fruity_interaction:
        result = m_fruity_interaction.on_button_press_event(a_ev, *this);
        break;
    case e_seq42_interaction:
        result = m_seq42_interaction.on_button_press_event(a_ev, *this);
        break;
    default:
        result = false;
    }
    return result;
}

bool
perfroll::on_button_release_event(GdkEventButton* a_ev)
{
    bool result;

    switch (global_interactionmethod)
    {
    case e_fruity_interaction:
        result = m_fruity_interaction.on_button_release_event(a_ev, *this);
        break;
    case e_seq42_interaction:
        result = m_seq42_interaction.on_button_release_event(a_ev, *this);
        break;
    default:
        result = false;
    }

    m_mainperf->set_follow_transport(transport_follow);
    trans_button_press = false;

    return result;
}

void
perfroll::auto_scroll_horz()
{
    if(!m_mainperf->get_follow_transport())
        return;

    if(m_zoom >= c_perf_scale_x)
    {
        double progress = (double)m_mainperf->get_tick()/m_zoom/c_ppen;

        int zoom_ratio = m_zoom/c_perf_scale_x;

        progress *= zoom_ratio;

        int offset = zoom_ratio;
        if(zoom_ratio != 1)
            offset *= -2;

        double page_size_adjust = (m_hadjust->get_page_size()/zoom_ratio)/2;
        double get_value_adjust = m_hadjust->get_value()*zoom_ratio;

        if(progress > page_size_adjust || (get_value_adjust > progress))
            m_hadjust->set_value(progress - page_size_adjust + offset);
        /*
        printf
        (
            "get_value [%f]: get_page_size[%f]: progress[%f]: page_size_adjust[%f]: get_value_adjust[%f] \n",
            m_hadjust->get_value(),
            m_hadjust->get_page_size(),
            progress,
            page_size_adjust,
            get_value_adjust
        );
        */
        return;
    }

    long progress_tick = m_mainperf->get_tick();
    long tick_offset = m_4bar_offset * c_ppqn * 16;

    int progress_x =     ( progress_tick - tick_offset ) / m_zoom  + 100;
    int page = progress_x / m_window_x;

    if (page != 0 || progress_x < 0)
    {
        double left_tick = (double) progress_tick /m_zoom/c_ppen;

        switch(m_zoom)
        {
//        case 4:
//            m_hadjust->set_value(left_tick / 8);
//            break;
        case 8:
            m_hadjust->set_value(left_tick / 4);
            break;
        case 16:
            m_hadjust->set_value(left_tick / 2 );
            break;
/*        case 32:
            m_hadjust->set_value(left_tick );
            break;
        case 64:
            m_hadjust->set_value(left_tick * 2 );
            break;
        case 128:
            m_hadjust->set_value(left_tick * 4 );
            break;*/
        default:
            break;
        }
    }
}

bool
perfroll::on_scroll_event( GdkEventScroll* a_ev )
{
    guint modifiers;    // Used to filter out caps/num lock etc.
    modifiers = gtk_accelerator_get_default_mod_mask ();

    if ((a_ev->state & modifiers) == GDK_CONTROL_MASK)
    {
        if (a_ev->direction == GDK_SCROLL_DOWN)
        {
            m_mainwnd->set_zoom(m_zoom*2);
        }
        else if (a_ev->direction == GDK_SCROLL_UP)
        {
            m_mainwnd->set_zoom(m_zoom/2);
        }
        return true;
    }

    if ((a_ev->state & modifiers) == GDK_SHIFT_MASK)
    {
        double val = m_hadjust->get_value();

        if ( a_ev->direction == GDK_SCROLL_UP )
        {
            val -= m_hadjust->get_step_increment();
        }
        else if ( a_ev->direction == GDK_SCROLL_DOWN )
        {
            val += m_hadjust->get_step_increment();
        }

        m_hadjust->clamp_page(val, val + m_hadjust->get_page_size());
    }
    else
    {
        double val = m_vadjust->get_value();

        if ( a_ev->direction == GDK_SCROLL_UP )
        {
            val -= m_vadjust->get_step_increment();
        }
        else if ( a_ev->direction == GDK_SCROLL_DOWN )
        {
            val += m_vadjust->get_step_increment();
        }

        m_vadjust->clamp_page(val, val + m_vadjust->get_page_size());
    }
    return true;
}

bool
perfroll::on_motion_notify_event(GdkEventMotion* a_ev)
{
    bool result;

    switch (global_interactionmethod)
    {
    case e_fruity_interaction:
        result = m_fruity_interaction.on_motion_notify_event(a_ev, *this);
        break;
    case e_seq42_interaction:
        result = m_seq42_interaction.on_motion_notify_event(a_ev, *this);
        if(global_interaction_method_change)
        {
            get_window()->set_cursor( Gdk::Cursor( Gdk::LEFT_PTR ));
            global_interaction_method_change = false;
        }
        break;
    default:
        result = false;
    }
    return result;
}

bool
perfroll::on_key_press_event(GdkEventKey* a_p0)
{
    if (a_p0->keyval == GDK_Z)         /* zoom in              */
    {
        m_mainwnd->set_zoom(m_zoom / 2);
        return true;
    }
    else if (a_p0->keyval == GDK_0)         /* reset to normal zoom */
    {
        m_mainwnd->set_zoom(c_perf_scale_x);
        return true;
    }
    else if (a_p0->keyval == GDK_z)         /* zoom out             */
    {
        m_mainwnd->set_zoom(m_zoom * 2);
        return true;
    }

    if (a_p0->keyval == m_mainperf->m_key_pointer)         /* Move to mouse position */
    {
        int x = 0;
        int y = 0;

        long a_tick = 0;

        get_pointer(x, y);
        if(x < 0)
            x = 0;
        snap_x(&x);
        convert_x(x, &a_tick);

        if(global_song_start_mode)
        {
            if(m_mainperf->is_jack_running())
            {
                m_mainperf->set_reposition();
                m_mainperf->set_starting_tick(a_tick);
                m_mainperf->position_jack(true, a_tick);
            }
            else
            {
                m_mainperf->set_reposition();
                m_mainperf->set_starting_tick(a_tick);
            }
        }

        return true;
    }

    bool ret = false;

    if ( m_mainperf->is_active_track( m_drop_track))
    {
        if ( a_p0->type == GDK_KEY_PRESS )
        {
            if ( a_p0->keyval ==  GDK_Delete || a_p0->keyval == GDK_BackSpace )
            {
                m_mainperf->push_trigger_undo(m_drop_track);
                m_mainperf->get_track( m_drop_track )->del_selected_trigger();

                ret = true;
            }

            if ( a_p0->state & GDK_CONTROL_MASK )
            {
                /* cut */
                if ( a_p0->keyval == GDK_x || a_p0->keyval == GDK_X )
                {
                    m_mainperf->push_trigger_undo(m_drop_track);
                    m_mainperf->get_track( m_drop_track )->cut_selected_trigger();
                    ret = true;
                }
                /* copy */
                if ( a_p0->keyval == GDK_c || a_p0->keyval == GDK_C )
                {
                    /* for cross-track trigger paste we need to clear all previous trigger copies
                       or the cross track routine will just grab the earliest one if leftover */

                    for ( int t=0; t<c_max_track; ++t )
                    {
                        if (! m_mainperf->is_active_track( t ))
                        {
                            continue;
                        }
                        m_mainperf->get_track(t)->unset_trigger_copied(); // clear previous copies
                    }
                    m_mainperf->get_track( m_drop_track )->copy_selected_trigger();
                    cross_track_paste = false;                            // clear for new cross track - one per copy to avoid unwanted duplication
                    ret = true;
                }

                /* paste */
                if ( a_p0->keyval == GDK_v || a_p0->keyval == GDK_V )
                {
                    if (m_mainperf->get_track(m_drop_track)->get_trigger_clipboard()->m_sequence >= 0)
                    {
                        m_mainperf->push_trigger_undo(m_drop_track);
                        m_mainperf->get_track( m_drop_track )->paste_trigger();
                        ret = true;
                    }
                }
            }
        }
    }

    if ( ret == true )
    {
        fill_background_pixmap();
        queue_draw();
        return true;
    }
    else
        return false;
}

/* performs a 'snap' on x */
void
perfroll::snap_x( int *a_x )
{
    // snap = number pulses to snap to
    // m_scale = number of pulses per pixel
    //	so snap / m_scale  = number pixels to snap to

    int mod = (m_snap / m_perf_scale_x );

    if ( mod <= 0 )
        mod = 1;

    *a_x = *a_x - (*a_x % mod );
}

void
perfroll::convert_x( int a_x, long *a_tick )
{
    long tick_offset = m_4bar_offset * c_ppqn * 16;
    *a_tick = a_x * m_perf_scale_x;
    *a_tick += tick_offset;
}

void
perfroll::convert_xy( int a_x, int a_y, long *a_tick, int *a_track)
{
    long tick_offset = m_4bar_offset * c_ppqn * 16;

    *a_tick = a_x * m_perf_scale_x;
    *a_track = a_y / c_names_y;

    *a_tick += tick_offset;
    *a_track  += m_track_offset;

    if ( *a_track >= c_max_track )
        *a_track = c_max_track - 1;

    if ( *a_track < 0 )
        *a_track = 0;
}

bool
perfroll::on_focus_in_event(GdkEventFocus*)
{
    set_flags(Gtk::HAS_FOCUS);
    return false;
}

bool
perfroll::on_focus_out_event(GdkEventFocus*)
{
    unset_flags(Gtk::HAS_FOCUS);
    return false;
}

void
perfroll::on_size_allocate(Gtk::Allocation& a_r )
{
    Gtk::DrawingArea::on_size_allocate( a_r );

    m_window_x = a_r.get_width();
    m_window_y = a_r.get_height();

    update_sizes();
}

void
perfroll::on_size_request(GtkRequisition* a_r )
{
}

void
perfroll::new_sequence( track *a_track, trigger *a_trigger )
{
    m_mainperf->push_track_undo(m_mainperf->get_track_index(a_track));
    int seq_idx = a_track->new_sequence();
    sequence *a_sequence = a_track->get_sequence(seq_idx);
    a_track->set_trigger_sequence(a_trigger, seq_idx);
    new seqedit( a_sequence, m_mainperf );
}

void
perfroll::copy_sequence( track *a_track, trigger *a_trigger, sequence *a_seq )
{
    m_mainperf->push_track_undo(m_mainperf->get_track_index(a_track));
    bool same_track = a_track == a_seq->get_track();
    int seq_idx = a_track->new_sequence();
    sequence *a_sequence = a_track->get_sequence(seq_idx);
    *a_sequence = *a_seq;
    a_sequence->set_track(a_track);
    if(same_track)
    {
        char new_name[c_max_seq_name+1];
        snprintf(new_name, sizeof(new_name), "%s copy", a_sequence->get_name());
        a_sequence->set_name( new_name );
    }
    a_track->set_trigger_sequence(a_trigger, seq_idx);
    new seqedit( a_sequence, m_mainperf );
}

void
perfroll::edit_sequence( track *a_track, trigger *a_trigger )
{
    sequence *a_seq = a_track->get_trigger_sequence(a_trigger);
    if(a_seq->get_editing())
    {
        a_seq->set_raise(true);
    }
    else
    {
        new seqedit( a_seq, m_mainperf );
    }
}

void
perfroll::export_sequence( track *a_track, trigger *a_trigger)
{
    sequence *a_seq = a_track->get_trigger_sequence(a_trigger);
    m_mainwnd->export_sequence_midi(a_seq);
}

void
perfroll::export_trigger( track *a_track, trigger *a_trigger)
{
    a_track->set_trigger_export(a_trigger);
    m_mainwnd->export_trigger_midi(a_track);
}

void perfroll::play_sequence( track *a_track, trigger *a_trigger)
{
    sequence *a_seq =  a_track->get_trigger_sequence(a_trigger);
    a_seq->set_playing(!a_seq->get_playing());
}

void perfroll::set_trigger_sequence( track *a_track, trigger *a_trigger, int a_sequence )
{
    m_mainperf->push_trigger_undo(m_mainperf->get_track_index(a_track));
    a_track->set_trigger_sequence(a_trigger, a_sequence);
}

void
perfroll::del_trigger( track *a_track, long a_tick )
{
    m_mainperf->push_trigger_undo(m_mainperf->get_track_index(a_track));
    a_track->del_trigger( a_tick );
    a_track->set_dirty( );
}

void
perfroll::paste_trigger_mouse(long a_tick)
{
    for ( int t=0; t<c_max_track; ++t )
    {
        if (! m_mainperf->is_active_track( t ))
        {
            continue;
        }

        if(m_mainperf->get_track(t)->get_trigger_copied() &&    // do we have a copy to clipboard
                !cross_track_paste)                             // has cross track paste NOT been done for this ctrl-c copy
        {
            if(t != m_drop_track)                               // If clipboard and paste are on diff tracks - then cross track paste
            {
                cross_track_paste = true;
                paste_trigger_sequence
                (
                    m_mainperf->get_track( m_drop_track ),
                    m_mainperf->get_track(t)->get_sequence(m_mainperf->get_track(t)
                            ->get_trigger_clipboard()->m_sequence),
                    a_tick
                );
                break;
            }
        }
    }

    if(!cross_track_paste) // then regular on track paste
    {
        if (m_mainperf->get_track(m_drop_track)->get_trigger_clipboard()->m_sequence >= 0)
        {
            m_mainperf->push_trigger_undo(m_drop_track);
            m_mainperf->get_track( m_drop_track )->paste_trigger(a_tick);
        }
   }
   //cross_track_paste = false; - should not do this. If user is not careful they will duplicate sequences everywhere!
   // we require the user to ctrl-c again after every cross track paste to avoid unnoticed duplication of sequences.
}


void
perfroll::paste_trigger_sequence( track *p_track, sequence *a_sequence, long a_tick )
{
    // empty trigger = segfault via get_length - don't allow w/o sequence
    if (a_sequence == NULL)
        return;

    if ( a_sequence->get_track()->get_trigger_copied() )
    {
        if(a_sequence->get_track() != p_track)  // then we have to copy the sequence
        {
            m_mainperf->push_track_undo(m_mainperf->get_track_index(p_track));
            // Add the sequence
            int seq_idx = p_track->new_sequence();
            sequence *seq = p_track->get_sequence(seq_idx);
            *seq = *a_sequence;
            seq->set_track(p_track);
            trigger *a_trigger = NULL;
            a_trigger = a_sequence->get_track()->get_trigger_clipboard();

            //printf("a_trigger-m_offset[%ld]\na_trigger-m_tick_start[%ld]\nm_tick_end[%ld]\n",a_trigger->m_offset,
            //       a_trigger->m_tick_start,a_trigger->m_tick_end);
            //put trigger into current track clipboard
            p_track->get_trigger_clipboard()->m_offset = a_trigger->m_offset;
            p_track->get_trigger_clipboard()->m_selected = a_trigger->m_selected;
            p_track->get_trigger_clipboard()->m_sequence = seq_idx;
            p_track->get_trigger_clipboard()->m_tick_start = a_trigger->m_tick_start;
            p_track->get_trigger_clipboard()->m_tick_end = a_trigger->m_tick_end;

            long length =  p_track->get_trigger_clipboard()->m_tick_end - p_track->get_trigger_clipboard()->m_tick_start + 1;

            long offset_adjust = a_tick - p_track->get_trigger_clipboard()->m_tick_start;
            p_track->add_trigger(a_tick, length,
                                 p_track->get_trigger_clipboard()->m_offset + offset_adjust,
                                 p_track->get_trigger_clipboard()->m_sequence); // +/- distance to paste tick from start

            p_track->get_trigger_clipboard()->m_tick_start = a_tick;
            p_track->get_trigger_clipboard()->m_tick_end = p_track->get_trigger_clipboard()->m_tick_start + length - 1;
            p_track->get_trigger_clipboard()->m_offset += offset_adjust;

            long a_length = p_track->get_sequence(p_track->get_trigger_clipboard()->m_sequence)->get_length();
            p_track->get_trigger_clipboard()->m_offset = p_track->adjust_offset(p_track->get_trigger_clipboard()->m_offset,a_length);

            //printf("p-m_offset[%ld]\np-m_tick_start[%ld]\np-m_tick_end[%ld]\n",p_track->get_trigger_clipboard()->m_offset,
            //       p_track->get_trigger_clipboard()->m_tick_start,p_track->get_trigger_clipboard()->m_tick_end);

            p_track->set_trigger_copied();  // change to paste track
            a_sequence->get_track()->unset_trigger_copied(); // undo original
        }
    }
}

void
perfroll::trigger_menu_popup(GdkEventButton* a_ev, perfroll& ths)
{
    using namespace Menu_Helpers;
    track *a_track = NULL;
    trigger *a_trigger = NULL;
    if ( ths.m_mainperf->is_active_track( ths.m_drop_track ))
    {
        a_track = ths.m_mainperf->get_track( ths.m_drop_track );
        a_trigger = a_track->get_trigger( ths.m_drop_tick );
    }
    if(a_trigger != NULL)
    {
        Menu *menu_trigger =   manage( new Menu());
        //menu_trigger->items().push_back(SeparatorElem());
        if(a_trigger->m_sequence > -1)
        {
            menu_trigger->items().push_back(MenuElem("Edit sequence", sigc::bind(mem_fun(ths,&perfroll::edit_sequence), a_track, a_trigger )));
            menu_trigger->items().push_back(MenuElem("Export sequence", sigc::bind(mem_fun(ths,&perfroll::export_sequence), a_track, a_trigger )));
            menu_trigger->items().push_back(MenuElem("Export trigger", sigc::bind(mem_fun(ths,&perfroll::export_trigger), a_track, a_trigger )));
        }

        if(a_trigger->m_sequence > -1 && !global_song_start_mode)
        {
            menu_trigger->items().push_back(MenuElem("Set/Unset playing", sigc::bind(mem_fun(ths,&perfroll::play_sequence), a_track, a_trigger )));
        }

        menu_trigger->items().push_back(MenuElem("New sequence", sigc::bind(mem_fun(ths,&perfroll::new_sequence), a_track, a_trigger )));
        if(a_track->get_number_of_sequences())
        {
            char name[40];
            Menu *set_seq_menu = manage( new Menu());
            for (unsigned s=0; s< a_track->get_number_of_sequences(); s++ )
            {
                sequence *a_seq = a_track->get_sequence( s );
                snprintf(name, sizeof(name),"[%d] %s", s+1, a_seq->get_name());
                set_seq_menu->items().push_back(MenuElem(name,
                                                sigc::bind(mem_fun(ths, &perfroll::set_trigger_sequence), a_track, a_trigger, s)));

            }
            menu_trigger->items().push_back(MenuElem("Set sequence", *set_seq_menu));
        }
        menu_trigger->items().push_back(MenuElem("Delete trigger", sigc::bind(mem_fun(ths,&perfroll::del_trigger), a_track, ths.m_drop_tick )));


        Menu *copy_seq_menu = NULL;
        char name[40];
        for ( int t=0; t<c_max_track; ++t )
        {
            if (! ths.m_mainperf->is_active_track( t ))
            {
                continue;
            }
            track *some_track = ths.m_mainperf->get_track(t);

            Menu *menu_t = NULL;
            bool inserted = false;
            for (unsigned s=0; s< some_track->get_number_of_sequences(); s++ )
            {
                if ( !inserted )
                {
                    if(copy_seq_menu == NULL)
                    {
                        copy_seq_menu = manage( new Menu());
                    }
                    inserted = true;
                    snprintf(name, sizeof(name), "[%d] %s", t+1, some_track->get_name());
                    menu_t = manage( new Menu());
                    copy_seq_menu->items().push_back(MenuElem(name, *menu_t));
                }

                sequence *a_seq = some_track->get_sequence( s );
                snprintf(name, sizeof(name),"[%d] %s", s+1, a_seq->get_name());
                menu_t->items().push_back(MenuElem(name,
                                                   sigc::bind(mem_fun(ths, &perfroll::copy_sequence), a_track, a_trigger, a_seq)));
            }
        }
        if(copy_seq_menu != NULL)
        {
            menu_trigger->items().push_back(MenuElem("Copy sequence", *copy_seq_menu));
        }

        menu_trigger->popup(0,0);
    }
}

void
perfroll::set_zoom (int a_zoom)
{
    if (m_mainwnd->zoom_check(a_zoom))
    {
        m_zoom = a_zoom;
        m_perf_scale_x = m_zoom;

        if (m_perf_scale_x == 0)
            m_perf_scale_x = 1;

        update_sizes();
        fill_background_pixmap();
    }
}

long
perfroll::get_default_trigger_length(perfroll& ths)
{
    //c_default_trigger_length_in_bars * 4 * c_ppqn
    float trig_ratio = (float) ths.m_mainperf->get_bp_measure() / (float)ths.m_mainperf->get_bw();
    //printf("trig_ratio [%f]\n",trig_ratio);

    long trig_measure = 4 * c_ppqn;
    long ret = (long) (trig_ratio * trig_measure);
    //printf("trig_length [%ld]\n", ret);

    return ret;
}
