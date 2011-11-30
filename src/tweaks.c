/** \file
 * Tweaks to default UI behavior
 */
#include "zebra.h"
#include "dryos.h"
#include "bmp.h"
#include "tasks.h"
#include "debug.h"
#include "menu.h"
#include "property.h"
#include "propvalues.h"
#include "config.h"
#include "gui.h"
#include "lens.h"
#include "math.h"

void display_gain_toggle(int dir);
void clear_lv_affframe();


#if defined(CONFIG_1100D)
#include "disable-this-module.h"
#endif

CONFIG_INT("dof.preview.sticky", dofpreview_sticky, 0);

static void
dofp_display( void * priv, int x, int y, int selected )
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"DOF Preview (photos): %s",
		dofpreview_sticky  ? "Sticky" : "Normal"
	);
}

int dofp_value = 0;
static void
dofp_set(int value)
{
	dofp_value = value;
	prop_request_change(PROP_DOF_PREVIEW_MAYBE, &value, 2);
}

static void 
dofp_lock(void* priv)
{
	dofp_set(1);
}

PROP_HANDLER(PROP_LAST_JOB_STATE)
{
	if (dofp_value) dofp_set(0);
	return prop_cleanup(token, property);
}

static void 
dofp_update()
{
	static int state = 0;
	// 0: allow 0->1, disallow 1->0 (first DOF press)
	// 1: allow everything => reset things (second DOF presss)
	
	static int old_value = 0;
	int d = dofpreview; // to avoid race condition
	
	//~ bmp_printf(FONT_MED, 0, 0, "DOFp: btn=%d old=%d state=%d hs=%d ", dofpreview, old_value, state, HALFSHUTTER_PRESSED);
	
	if (dofpreview_sticky == 1)
	{
		if (d) {bmp_printf(FONT_LARGE, 720-font_large.width*3, 50, "DOF"); card_led_on(); }
		else if (old_value) { redraw(); card_led_off(); }
		
		if (d != old_value) // transition
		{
			if (state == 0)
			{
				if (old_value && !d)
				{
					//~ beep();
					dofp_set(1);
					msleep(100);
					state = 1;
				}
			}
			else
			{
				if (d == 0) state = 0;
				//~ beep();
			}
		}
		old_value = d;
	}
}


// ExpSim
//**********************************************************************
CONFIG_INT( "expsim", expsim_setting, 2);

static void set_expsim( int x )
{
	if (expsim != x)
	{
		prop_request_change(PROP_LIVE_VIEW_VIEWTYPE, &x, 4);
	}
}
/*
static void
expsim_toggle( void * priv )
{
	// off, on, auto
	if (!expsim_auto && !expsim) // off->on
	{
		set_expsim(1);
	}
	else if (!expsim_auto && expsim) // on->auto
	{
		expsim_auto = 1;
	}
	else // auto->off
	{
		expsim_auto = 0;
		set_expsim(0);
	}
}*/
int get_expsim_auto_value()
{
	extern int bulb_ramp_calibration_running; 
	if (bulb_ramp_calibration_running) 
		return 0; // temporarily disable ExpSim to make sure display gain will work
	
	#ifdef CONFIG_50D
	if (is_movie_mode()) return expsim_setting;
	#else
	if (is_movie_mode()) return 2;
	#endif

	// underexposure bug with manual lenses in M mode
	#if defined(CONFIG_60D)
	if (expsim_setting == 2 &&
		shooting_mode == SHOOTMODE_M && 
		!lens_info.name[0] && 
		lens_info.raw_iso != 0 && 
		lens_info.raw_shutter < 93 // the image will be too dark to preview via BV mode, better turn off ExpSim
	)
		return 0;
	#endif

	// silent pic in matrix mode requires expsim on
	extern int silent_pic_sweep_running;
	if (silent_pic_sweep_running) return 1;
	
	if (expsim_setting == 2)
	{
		if ((lv_dispsize > 1 || should_draw_zoom_overlay()) && !get_halfshutter_pressed()) return 0;
		else return 1;
	}
	else return expsim_setting;
}

static void
expsim_display( void * priv, int x, int y, int selected )
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Exp.Sim     : %s",
		expsim == 2 ? "Movie" :
		expsim_setting == 2 ? (get_expsim_auto_value() ? "Auto (ON)" : "Auto (OFF)") : 
		get_expsim_auto_value() ? "ON" : "OFF"
	);
	if (CONTROL_BV) menu_draw_icon(x, y, MNI_WARNING, (intptr_t) "Exposure override is active.");
	else if (!lv) menu_draw_icon(x, y, MNI_WARNING, (intptr_t) "This option works only in LiveView");
	else menu_draw_icon(x, y, expsim == 2 ? MNI_AUTO : expsim != get_expsim_auto_value() ? MNI_WARNING : expsim_setting == 2 ? MNI_AUTO : MNI_BOOL(expsim), (intptr_t) "Could not set ExpSim");
}

static void expsim_update()
{
	if (!lv) return;
	if (shooting_mode == SHOOTMODE_MOVIE) return;
	int expsim_auto_value = get_expsim_auto_value();
	if (expsim_auto_value != expsim)
	{
		if (MENU_MODE && !gui_menu_shown()) // ExpSim changed from Canon menu
		{ 
			expsim_setting = expsim;
			NotifyBox(2000, "ML: Auto ExpSim disabled.");
		}
		else
		{
			set_expsim(expsim_auto_value); // shooting mode, ML decides to toggle ExpSim
		}
	}
}

static void expsim_toggle(void* priv)
{
	if (is_movie_mode()) return;
	menu_ternary_toggle(priv, 1); msleep(100);
	menu_show_only_selected();
}

static void expsim_toggle_reverse(void* priv)
{
	if (is_movie_mode()) return;
	menu_ternary_toggle_reverse(priv, -1); msleep(100);
	menu_show_only_selected();
}

// LV metering
//**********************************************************************

CONFIG_INT("lv.metering", lv_metering, 0);

static void
lv_metering_print( void * priv, int x, int y, int selected )
{
	//unsigned z = *(unsigned*) priv;
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"LV Auto ISO (M mode): %s",
		lv_metering == 0 ? "OFF" :
		lv_metering == 1 ? "Spotmeter" :
		lv_metering == 2 ? "CenteredHist" :
		lv_metering == 3 ? "HighlightPri" :
		lv_metering == 4 ? "NoOverexpose" : "err"
	);
	if (lv_metering)
	{
		if (shooting_mode != SHOOTMODE_M || !lv)
			menu_draw_icon(x, y, MNI_WARNING, (intptr_t) "Only works in photo mode (M), LiveView");
		if (!expsim)
			menu_draw_icon(x, y, MNI_WARNING, (intptr_t) "ExpSim is OFF");
	}
}

static void
shutter_alter( int sign)
{
	#ifdef CONFIG_60D
	sign *= 3;
	#endif
	
	if (AE_VALUE > 5*8 && sign < 0) return;
	if (AE_VALUE < -5*8 && sign > 0) return;
	
	int rs = lens_info.raw_shutter;
	//~ for (int k = 0; k < 8; k++)
	{
		rs += sign;
		lens_set_rawshutter(rs);
		msleep(10);
		if (lens_info.raw_shutter == rs) return;
	}
}

static void
iso_alter( int sign)
{
	sign = -sign;
	sign *= 8;
	
	if (AE_VALUE > 5*8 && sign < 0) return;
	if (AE_VALUE < -5*8 && sign > 0) return;
	
	int ri = lens_info.raw_iso;
	//~ for (int k = 0; k < 8; k++)
	{
		ri += sign;
		ri = MIN(ri, 120); // max ISO 6400
		lens_set_rawiso(ri);
		msleep(10);
		if (lens_info.raw_iso == ri) return;
	}
}

static void
lv_metering_adjust()
{
	if (!lv) return;
	if (!expsim) return;
	if (gui_menu_shown()) return;
	if (!liveview_display_idle()) return;
	if (ISO_ADJUSTMENT_ACTIVE) return;
	if (get_halfshutter_pressed()) return;
	if (lv_dispsize != 1) return;
	//~ if (shooting_mode != SHOOTMODE_P && shooting_mode != SHOOTMODE_AV && shooting_mode != SHOOTMODE_TV) return;
	if (shooting_mode != SHOOTMODE_M) return;
	
	if (lv_metering == 1)
	{
		int Y,U,V;
		get_spot_yuv(5, &Y, &U, &V);
		//bmp_printf(FONT_LARGE, 0, 100, "Y %d AE %d  ", Y, lens_info.ae);
		iso_alter(SGN(Y-128));
	}
	else if (lv_metering == 2) // centered histogram
	{
		int under, over;
		get_under_and_over_exposure_autothr(&under, &over);
		//~ bmp_printf(FONT_MED, 10, 40, "over=%d under=%d ", over, under);
		if (over > under) iso_alter(1);
		else iso_alter(-1);
	}
	else if (lv_metering == 3) // highlight priority
	{
		int under, over;
		get_under_and_over_exposure(10, 235, &under, &over);
		//~ bmp_printf(FONT_MED, 10, 40, "over=%d under=%d ", over, under);
		if (over > 100 && under < over * 5) iso_alter(1);
		else iso_alter(-1);
	}
	else if (lv_metering == 4) // don't overexpose
	{
		int under, over;
		get_under_and_over_exposure(5, 235, &under, &over);
		//~ bmp_printf(FONT_MED, 10, 40, "over=%d ", over);
		if (over > 100) iso_alter(1);
		else iso_alter(-1);
	}
	msleep(500);
	//~ bee
	//~ beep();
}

// auto burst pic quality
//**********************************************************************

CONFIG_INT("burst.auto.picquality", auto_burst_pic_quality, 0);

static void set_pic_quality(int q)
{
	if (q == -1) return;
	prop_request_change(PROP_PIC_QUALITY, &q, 4);
	prop_request_change(PROP_PIC_QUALITY2, &q, 4);
	prop_request_change(PROP_PIC_QUALITY3, &q, 4);
}

int picq_saved = -1;
static void decrease_pic_quality()
{
	if (picq_saved == -1) picq_saved = pic_quality; // save only first change
	
	int newpicq = 0;
 	switch(pic_quality)
 	{
		case PICQ_RAW_JPG_LARGE_FINE:
 			newpicq = PICQ_LARGE_FINE;
 			break;
 		case PICQ_RAW:
			newpicq = PICQ_LARGE_FINE;
 			break;
 		case PICQ_LARGE_FINE:
 			newpicq = PICQ_MED_FINE;
 			break;
		//~ case PICQ_MED_FINE:
			//~ newpicq = PICQ_SMALL_FINE;
			//~ break;
 		//~ case PICQ_SMALL_FINE:
 			//~ newpicq = PICQ_SMALL_COARSE;
 			//~ break;
 		case PICQ_LARGE_COARSE:
 			newpicq = PICQ_MED_COARSE;
 			break;
		//~ case PICQ_MED_COARSE:
			//~ newpicq = PICQ_SMALL_COARSE;
			//~ break;
 	}
 	if (newpicq) set_pic_quality(newpicq);
}
 
static void restore_pic_quality()
{
	if (picq_saved != -1) set_pic_quality(picq_saved);
	picq_saved = -1;
}

static void adjust_burst_pic_quality()
{
	if (lens_info.job_state == 0) { restore_pic_quality(); return; }
	if (burst_count < 4) decrease_pic_quality();
	else if (burst_count >= 5) restore_pic_quality();
}

#if !defined(CONFIG_60D) && !defined(CONFIG_50D) && !defined(CONFIG_5D2)
PROP_HANDLER(PROP_BURST_COUNT)
{
	int burst_count = buf[0];

	if (auto_burst_pic_quality && avail_shot > burst_count)
	{
		adjust_burst_pic_quality();
	}

	return prop_cleanup(token, property);
}
#endif

static void
auto_burst_pic_display(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Auto BurstPicQuality: %s", 
		auto_burst_pic_quality ? "ON" : "OFF"
	);
}

void lcd_sensor_shortcuts_print( void * priv, int x, int y, int selected);
extern unsigned lcd_sensor_shortcuts;

// backlight adjust
//**********************************************************************

int display_gain = 0;

void show_display_gain()
{
	int gain_ev = 0;
	if (display_gain) gain_ev = gain_to_ev(display_gain) - 10;
	NotifyBox(2000, "Display Gain : %s%d EV", display_gain ? "+" : "", gain_ev);
	redraw();
}

void adjust_backlight_level(int delta)
{
	if (backlight_level < 1 || backlight_level > 7) return; // kore wa dame desu yo
	if (tft_status) call("TurnOnDisplay");
	
	// if we run out of backlight, adjust display gain instead
	
	if (lv)
	{
		if (backlight_level == 7 && delta > 0)
		{
			beep();
			display_gain_toggle(1);
			show_display_gain();
			return;
		}

		if (backlight_level == 7 && delta < 0 && display_gain)
		{
			beep();
			display_gain_toggle(-1);
			show_display_gain();
			return;
		}
	}
	
	int level = COERCE(backlight_level + delta, 1, 7);
	prop_request_change(PROP_BACKLIGHT_LEVEL, &level, 4);
	NotifyBoxHide();
	NotifyBox(2000, "LCD Backlight: %d", level);
}
void set_backlight_level(int level)
{
	level = COERCE(level, 1, 7);
	prop_request_change(PROP_BACKLIGHT_LEVEL, &level, 4);
}

CONFIG_INT("af.frame.autohide", af_frame_autohide, 1);

static void
af_frame_autohide_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"AF frame display    : %s", 
		af_frame_autohide ? "AutoHide" : "Show"
	);
	menu_draw_icon(x, y, MNI_BOOL_LV(af_frame_autohide));
}

int afframe_countdown = 0;
void afframe_set_dirty()
{
	afframe_countdown = 20;
}
void afframe_clr_dirty()
{
	afframe_countdown = 0;
}

// this should be thread safe
void clear_lv_affframe_if_dirty()
{
	//~ #ifndef CONFIG_50D
	if (af_frame_autohide && afframe_countdown && liveview_display_idle())
	{
		BMP_LOCK (
			if (afframe_countdown)
			{
				afframe_countdown--;
				if (!afframe_countdown) 
					clear_lv_affframe();
			}
		)
	}
	//~ #endif
}

void clear_lv_affframe()
{
	if (!lv) return;
	if (gui_menu_shown()) return;
	if (lv_dispsize != 1) return;
	struct vram_info *	lv = get_yuv422_vram();
	if( !lv->vram )	return;
	int xaf,yaf;
	
	get_afframe_pos(720, 480, &xaf, &yaf);
	xaf = N2BM_X(xaf);
	yaf = N2BM_Y(yaf);
	//~ bmp_printf(FONT_LARGE, 200, 200, "af %d %d ", xaf, yaf);
	bmp_fill(0, COERCE(xaf,100, BMP_WIDTH-100) - 100, COERCE(yaf,100, BMP_HEIGHT-100) - 100, 200, 200 );
	crop_set_dirty(5);
	afframe_countdown = 0;
}

CONFIG_INT("play.quick.zoom", quickzoom, 1);

static void
quickzoom_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Zoom in PLAY mode : %s", 
		quickzoom == 0 ? "Normal" :
		quickzoom == 1 ? "Fast+100%" :
		quickzoom == 2 ? "Fast" : "err"
	);
}

#ifdef CONFIG_60D

CONFIG_INT("display.off.halfshutter", display_off_by_halfshutter_enabled, 0);

int display_turned_off_by_halfshutter = 0; // 1 = display was turned off, -1 = display should be turned back on (ML should take action)

PROP_INT(PROP_INFO_BUTTON_FUNCTION, info_button_function);

static void display_on_and_go_to_main_shooting_screen()
{
	if (lv) return;
	if (tft_status == 0) return; // display already on
	if (gui_state != GUISTATE_IDLE) return;
	
	display_turned_off_by_halfshutter = 0;
	
	int old = info_button_function;
	int x = 1;
	//~ card_led_blink(5,100,100);
	prop_request_change(PROP_INFO_BUTTON_FUNCTION, &x, 4); // temporarily make the INFO button display only the main shooting screen
	fake_simple_button(BGMT_DISP);
	msleep(300);
	prop_request_change(PROP_INFO_BUTTON_FUNCTION, &old, 4); // restore user setting back
}

int handle_disp_button_in_photo_mode() // called from handle_buttons
{
	if (display_off_by_halfshutter_enabled && display_turned_off_by_halfshutter == 1 && gui_state == GUISTATE_IDLE && !gui_menu_shown())
	{
		display_turned_off_by_halfshutter = -1; // request: ML should turn it on
		return 0;
	}
	return 1;
}

static void display_off_by_halfshutter()
{
	static int prev_gui_state = 0;
	if (prev_gui_state != GUISTATE_IDLE) 
	{ 
		msleep(100);
		prev_gui_state = gui_state;
		while (get_halfshutter_pressed()) msleep(100);
		return; 
	}
	prev_gui_state = gui_state;
		
	if (!lv && gui_state == GUISTATE_IDLE) // main shooting screen, photo mode
	{
		if (tft_status == 0) // display is on
		{
			if (get_halfshutter_pressed())
			{
				// wait for long half-shutter press (1 second)
				int i;
				for (i = 0; i < 10; i++)
				{
					msleep(100);
					if (!get_halfshutter_pressed()) return;
					if (tft_status) return;
				}
				fake_simple_button(BGMT_DISP); // turn display off
				while (get_halfshutter_pressed()) msleep(100);
				display_turned_off_by_halfshutter = 1; // next INFO press will go to main shooting screen
				return;
			}
		}
		else // display is off
		{
			if (display_turned_off_by_halfshutter == -1)
			{
				display_on_and_go_to_main_shooting_screen();
				display_turned_off_by_halfshutter = 0;
			}
		}
	}
}

static void
display_off_by_halfshutter_print(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"DispOFF(Photo) : %s", // better name for this?
		display_off_by_halfshutter_enabled ? "HalfShutter" : "OFF"
	);
	if (display_off_by_halfshutter_enabled && lv)
		menu_draw_icon(x, y, MNI_WARNING, (intptr_t)"This option does not work in LiveView");
}

#endif

CONFIG_INT("crop.playback", cropmarks_play, 0);

static void
cropmarks_play_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Cropmarks   (PLAY): %s", 
		cropmarks_play ? "ON" : "OFF"
	);
}

CONFIG_INT("play.set.wheel", play_set_wheel_action, 2);

static void
play_set_wheel_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"SET+MainDial(PLAY): %s", 
		play_set_wheel_action == 0 ? "422 Preview" :
		play_set_wheel_action == 1 ? "ExposureFusion" : 
		play_set_wheel_action == 2 ? "CompareImages" : 
		play_set_wheel_action == 3 ? "TimelapsePlay" : "err"
	);
	menu_draw_icon(x, y, MNI_ON, 0);
}

CONFIG_INT("quick.delete", quick_delete, 0);
static void
quick_delete_print(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Quick Erase       : %s", 
		quick_delete ? "SET+Erase" : "OFF"
	);
}

int timelapse_playback = 0;

void playback_set_wheel_action(int dir)
{
	if (play_set_wheel_action == 0) play_next_422(dir);
	else if (play_set_wheel_action == 1) expfuse_preview_update(dir);
	else if (play_set_wheel_action == 2) playback_compare_images(dir);
	else if (play_set_wheel_action == 3) timelapse_playback += dir;
}

int handle_set_wheel_play(struct event * event)
{
	if (gui_menu_shown()) return 1;
	
	extern int set_pressed;
	// SET button pressed
	//~ if (event->param == BGMT_PRESS_SET) set_pressed = 1;
	//~ if (event->param == BGMT_UNPRESS_SET) set_pressed = 0;
	//~ if (event->param == BGMT_PLAY) set_pressed = 0;

	// reset exposure fusion preview
	extern int expfuse_running;
	if (set_pressed == 0)
	{
		expfuse_running = 0;
	}

	// SET+Wheel action in PLAY mode
	if ( PLAY_MODE && get_set_pressed())
	{
		if (!IS_FAKE(event) && (event->param == BGMT_WHEEL_LEFT || event->param == BGMT_WHEEL_RIGHT))
		{
			int dir = event->param == BGMT_WHEEL_RIGHT ? 1 : -1;
			playback_set_wheel_action(dir);
			return 0;
		}
		
		if (quick_delete)
		{
			if (event->param == BGMT_TRASH)
			{
				fake_simple_button(BGMT_UNPRESS_SET);
				fake_simple_button(BGMT_TRASH);
				fake_simple_button(BGMT_WHEEL_DOWN);
				fake_simple_button(BGMT_PRESS_SET);
				return 0;
			}
		}
	}
	
	return 1;
}

CONFIG_INT("play.lv.button", play_lv_action, 1);

static void
play_lv_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"LV button   (PLAY): %s", 
		play_lv_action == 0 ? "Default" :
		play_lv_action == 1 ? "Protect Image" : "err"
	);
}

#if defined(CONFIG_60D) || defined(CONFIG_600D)
int handle_lv_play(struct event * event)
{
	if (!play_lv_action) return 1;
	
	if (event->param == BGMT_LV && PLAY_MODE)
	{
		fake_simple_button(BGMT_Q); // toggle protect current image
		fake_simple_button(BGMT_WHEEL_DOWN);
		fake_simple_button(BGMT_Q);
		return 0;
	}
	return 1;
}
#endif


// don't save it in config file, it's easy to forget it activated
int fake_halfshutter = 0;

static void
fake_halfshutter_print(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Shutter Half-Press  : %s",
		fake_halfshutter == 1 ? "Sticky" : 
		fake_halfshutter == 2 ? "Every second" : 
		fake_halfshutter == 3 ? "Every 200ms" : 
		fake_halfshutter == 4 ? "Every 20ms" : 
		"OFF"
	);
}

void hs_show()
{
	bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, COLOR_RED), 720-font_large.width*3, 50, "HS");
}
void hs_hide()
{
	bmp_printf(FONT(FONT_LARGE, COLOR_WHITE, 0), 720-font_large.width*3, 50, "  ");
}

void 
fake_halfshutter_step()
{
	if (gui_menu_shown()) return;
	if (fake_halfshutter >= 2)
	{
		if (gui_state == GUISTATE_IDLE && !gui_menu_shown() && !get_halfshutter_pressed())
		hs_show();
		SW1(1,5);
		SW1(0,0);
		hs_hide();
		if (fake_halfshutter == 2) msleep(1000);
		else if (fake_halfshutter == 3) msleep(200);
		else msleep(20);
	}
	else if (fake_halfshutter == 1) // sticky
	{
		static int state = 0;
		// 0: allow 0->1, disallow 1->0 (first press)
		// 1: allow everything => reset things (second presss)

		static int old_value = 0;
		int hs = HALFSHUTTER_PRESSED;
		
		if (hs) hs_show();
		else if (old_value) redraw();
		
		if (hs != old_value) // transition
		{
			if (state == 0)
			{
				if (old_value && !hs)
				{
					card_led_on();
					SW1(1,50);
					state = 1;
				}
			}
			else
			{
				if (hs == 0) { state = 0; card_led_off(); }
			}
		}
		old_value = hs;
	}
}

static void
tweak_task( void* unused)
{
	do_movie_mode_remap();
	
	int k;
	for (k = 0; ; k++)
	{
		if (fake_halfshutter)
			fake_halfshutter_step(); // this one should msleep as needed
		else
			msleep(50);
		
		if (lv_metering && !is_movie_mode() && lv && k % 5 == 0)
		{
			lv_metering_adjust();
		}
		
		// timelapse playback
		if (timelapse_playback)
		{
			if (!PLAY_MODE) { timelapse_playback = 0; continue; }
			
			//~ NotifyBox(1000, "Timelapse...");
			fake_simple_button(timelapse_playback > 0 ? BGMT_WHEEL_DOWN : BGMT_WHEEL_UP);
			continue;
		}
		
		// faster zoom in play mode
		if (quickzoom && PLAY_MODE)
		{
			if (get_zoom_in_pressed()) 
			{
				for (int i = 0; i < 10; i++)
				{
					if (quickzoom == 1 && PLAY_MODE && MEM(IMGPLAY_ZOOM_LEVEL_ADDR) <= 1)
					{
						MEM(IMGPLAY_ZOOM_LEVEL_ADDR) = IMGPLAY_ZOOM_LEVEL_MAX-1;
						MEM(IMGPLAY_ZOOM_LEVEL_ADDR + 4) = IMGPLAY_ZOOM_LEVEL_MAX-1;
					}
					msleep(30);
				}
				while (get_zoom_in_pressed()) {	fake_simple_button(BGMT_PRESS_ZOOMIN_MAYBE); msleep(50); }
			}
			
			if (get_zoom_out_pressed())
			{
				msleep(300);
				while (get_zoom_out_pressed()) {	fake_simple_button(BGMT_PRESS_ZOOMOUT_MAYBE); msleep(50); }
			}
		}
		
		expsim_update();
		
		dofp_update();

		clear_lv_affframe_if_dirty();
		
		extern unsigned disp_profiles_0;
		if (FLASH_BTN_MOVIE_MODE)
		{
			int k = 0;
			int longpress = 0;
			if (disp_profiles_0) bmp_printf(FONT_MED, 245, 100, "DISP preset toggle");
			while (FLASH_BTN_MOVIE_MODE)
			{
				msleep(100);
				k++;
				
				if (k > 3)
				{
					longpress = 1; // long press doesn't toggle
					if (disp_profiles_0) bmp_printf(FONT(FONT_MED, 1, 0), 245, 100, "                  ");
				}
				BMP_LOCK( draw_ml_bottombar(0,0); )
			}
			if (disp_profiles_0 && !longpress) toggle_disp_mode();
			redraw();
		}
		
		#ifdef CONFIG_60D
		if (display_off_by_halfshutter_enabled)
			display_off_by_halfshutter();
		#endif
		
		upside_down_step();
		
		if ((lv_disp_mode == 0 && LV_BOTTOM_BAR_DISPLAYED) || ISO_ADJUSTMENT_ACTIVE)
			idle_wakeup_reset_counters();
	}
}

TASK_CREATE("tweak_task", tweak_task, 0, 0x1e, 0x1000 );

extern int quick_review_allow_zoom;

static void
qrplay_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"After taking a pic: %s", 
		quick_review_allow_zoom ? "Hold->Play" : "QuickReview"
	);
}
/*
extern int set_on_halfshutter;

static void
set_on_halfshutter_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"HalfShutter in DLGs : %s", 
		set_on_halfshutter ? "SET" : "Cancel"
	);
}*/

extern int iso_round_only;
static void
iso_round_only_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"ISO selection       : %s", 
		iso_round_only ? "100x, 160x" : "All values"
	);
	menu_draw_icon(x, y, iso_round_only ? MNI_ON : MNI_AUTO, 0);
}


CONFIG_INT("swap.menu", swap_menu, 0);
static void
swap_menu_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Swap MENU <-> ERASE : %s", 
		swap_menu ? "ON" : "OFF"
	);
}

int handle_swap_menu_erase(struct event * event)
{
	if (swap_menu && !IS_FAKE(event))
	{
		if (event->param == BGMT_TRASH)
		{
			fake_simple_button(BGMT_MENU);
			return 0;
		}
		if (event->param == BGMT_MENU)
		{
			fake_simple_button(BGMT_TRASH);
			return 0;
		}
	}
	return 1;
}

/*extern int picstyle_disppreset_enabled;
static void
picstyle_disppreset_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"PicSty->DISP preset : %s",
		picstyle_disppreset_enabled ? "ON" : "OFF"
	);
}*/

extern unsigned display_dont_mirror;
static void
display_dont_mirror_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Auto Mirroring : %s", 
		display_dont_mirror ? "Don't allow": "Allow"
	);
	menu_draw_icon(x, y, MNI_BOOL(!display_dont_mirror), 0);
}

/*
int night_vision = 0;
void night_vision_toggle(void* priv)
{
	night_vision = !night_vision;
	call("lvae_setdispgain", night_vision ? 65535 : 0);
	menu_show_only_selected();
}

static void night_vision_print(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Night Vision Mode   : %s", 
		night_vision ? "ON" : "OFF"
	);
	if (night_vision && (!lv || is_movie_mode()))
		menu_draw_icon(x, y, MNI_WARNING, 0);
}
*/

void set_display_gain(int display_gain)
{
	call("lvae_setdispgain", COERCE(display_gain, 0, 65535));
}

// 1024 = 0 EV
// +: off, +1EV, +2EV, ... , +6EV, -3EV, -2EV, -1EV, off
// -: reverse
void display_gain_toggle(int dir)
{
	int d = display_gain;
	if (!d) d = 1024;
	 
	if (dir > 0)
	{
		display_gain = d * 2;
		if (display_gain > 65536) display_gain = 128;
	}
	else if (dir < 0)
	{
		if (d <= 128) display_gain = 65536;
		else display_gain = d / 2; 
	}
	else display_gain = 0;

	if (display_gain == 1024) display_gain = 0;

	set_display_gain(display_gain);
	menu_show_only_selected();
}
void display_gain_toggle_forward(void* priv) { display_gain_toggle(1); }
void display_gain_toggle_reverse(void* priv) { display_gain_toggle(-1); }
void display_gain_reset(void* priv) { display_gain_toggle(0); }

int gain_to_ev(int gain)
{
	if (gain == 0) return 0;
	return (int) roundf(log2f(gain));
}

static void display_gain_print(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	int gain_ev = 0;
	if (display_gain) gain_ev = gain_to_ev(display_gain) - 10;
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"LV Disp.Gain: %s%d EV",
		gain_ev > 0 ? "+" : gain_ev < 0 ? "-" : "",
		ABS(gain_ev)
	);
	if (display_gain)
	{
		if (CONTROL_BV) menu_draw_icon(x, y, MNI_WARNING, (intptr_t) "Exposure override is active.");
		else if (!lv) menu_draw_icon(x, y, MNI_WARNING, (intptr_t) "This option works only in LiveView");
		else menu_draw_icon(x, y, MNI_PERCENT, gain_ev * 100 / 6);
	}
}

/*
PROP_INT(PROP_ELECTRIC_SHUTTER, eshutter);

static void eshutter_display(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	int gain_ev = 0;
	if (display_gain) gain_ev = gain_to_ev(display_gain) - 10;
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Electric Shutter    : %d",
		eshutter
	);
}
static void set_eshutter(int e)
{
	e = COERCE(e, 0, 2);
	call("lv_eshutter", e);
	eshutter = e;
	GUI_SetElectricShutter(e);
	//~ prop_request_change(PROP_ELECTRIC_SHUTTER, &e, 4);
}
static void eshutter_toggle(void* priv)
{
	set_eshutter(mod(eshutter + 1, 3));
}*/

CONFIG_INT("digital.zoom.shortcut", digital_zoom_shortcut, 1);

void digital_zoom_shortcut_display(
        void *                  priv,
        int                     x,
        int                     y,
        int                     selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"DigitalZoom Shortcut: %s",
		digital_zoom_shortcut ? "1x, 3x" : "3x...10x"
	);
}

struct menu_entry tweak_menus[] = {
/*	{
		.name = "Night Vision Mode",
		.priv = &night_vision, 
		.select = night_vision_toggle, 
		.display = night_vision_print,
		.help = "Maximize LV display gain for framing in darkness (photo)"
	},*/
	{
		.name = "DOF Preview", 
		.priv = &dofpreview_sticky, 
		.select = menu_binary_toggle, 
		.select_auto = dofp_lock,
		.display = dofp_display,
		.help = "Sticky = click DOF to toggle. Or, press [Q] to lock now.",
	},
	{
		.name		= "Half-press shutter",
		.priv = &fake_halfshutter,
		.select		= menu_quinternary_toggle,
		.select_reverse = menu_quinternary_toggle_reverse,
		.display	= fake_halfshutter_print,
		.help = "Emulates half-shutter press, or make half-shutter sticky."
	},
	/*{
		.name = "Electric Shutter",
		.priv = &eshutter,
		.select = eshutter_toggle,
		.display = eshutter_display,
		.help = "For enabling third-party flashes in LiveView."
	},*/
	{
		.name = "AF frame display",
		.priv = &af_frame_autohide, 
		.select = menu_binary_toggle,
		.display = af_frame_autohide_display,
		.help = "You can hide the AF frame (the little white rectangle)."
	},
	#if defined(CONFIG_550D) || defined(CONFIG_500D)
	{
		.name = "LCD Sensor Shortcuts",
		.priv		= &lcd_sensor_shortcuts,
		.select		= menu_binary_toggle,
		.display	= lcd_sensor_shortcuts_print,
	},
	#endif
	#if !defined(CONFIG_60D) && !defined(CONFIG_50D) && !defined(CONFIG_5D2) // 60D doesn't need this
	{
		.name = "Auto BurstPicQuality",
		.priv = &auto_burst_pic_quality, 
		.select = menu_binary_toggle, 
		.display = auto_burst_pic_display,
	},
	#endif
	#if 0
	{
		.name = "HalfShutter in DLGs",
		.priv = &set_on_halfshutter, 
		.select = menu_binary_toggle, 
		.display = set_on_halfshutter_display,
		.help = "Half-shutter press in dialog boxes => OK (SET) or Cancel."
	},
	#endif
	{
		.name = "ISO selection",
		.priv = &iso_round_only,
		.display	= iso_round_only_display,
		.select		= menu_binary_toggle,
		.help = "You can enable only ISOs which are multiple of 100 and 160."
	},
	/*{
		.name = "PicSty->DISP preset",
		.priv = &picstyle_disppreset_enabled,
		.display	= picstyle_disppreset_display,
		.select		= menu_binary_toggle,
		.help = "PicStyle can be included in DISP preset for easy toggle."
	},*/
	#ifdef CONFIG_60D
	{
		.name = "Swap MENU <--> ERASE",
		.priv = &swap_menu,
		.display	= swap_menu_display,
		.select		= menu_binary_toggle,
		.help = "Swaps MENU and ERASE buttons."
	},
	#endif
	{
		.name = "LV Auto ISO (M mode)",
		.priv = &lv_metering,
		.select = menu_quinternary_toggle, 
		.select_reverse = menu_quinternary_toggle_reverse, 
		.display = lv_metering_print,
		.help = "Experimental LV metering (Auto ISO). Too slow for real use."
	},
	#ifdef CONFIG_600D
	{
		.name = "DigitalZoom Shortcut",
		.priv = &digital_zoom_shortcut,
		.display = digital_zoom_shortcut_display, 
		.select = menu_binary_toggle,
		.help = "Movie: DISP + Zoom In toggles between 1x and 3x modes."
	},
	#endif
};



extern int menu_upside_down;
static void menu_upside_down_print(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"UpsideDown mode: %s",
		menu_upside_down ? "ON" : "OFF"
	);
}

void NormalDisplay();
void MirrorDisplay();
void ReverseDisplay();

// reverse arrow keys
int handle_upside_down(struct event * event)
{
	// only reverse first arrow press
	// then wait for unpress event (or different arrow press) before reversing again
	
	extern int menu_upside_down;
	static int last_arrow_faked = 0;

	if (menu_upside_down && !IS_FAKE(event) && last_arrow_faked)
	{
		switch (event->param)
		{
			#ifdef BGMT_UNPRESS_UDLR
			case BGMT_UNPRESS_UDLR:
			#else
			case BGMT_UNPRESS_LEFT:
			case BGMT_UNPRESS_RIGHT:
			case BGMT_UNPRESS_UP:
			case BGMT_UNPRESS_DOWN:
			#endif
				last_arrow_faked = 0;
				return 1;
		}
	}

	if (menu_upside_down && !IS_FAKE(event) && last_arrow_faked != event->param)
	{
		switch (event->param)
		{
			case BGMT_PRESS_LEFT:
				last_arrow_faked = BGMT_PRESS_RIGHT;
				break;
			case BGMT_PRESS_RIGHT:
				last_arrow_faked = BGMT_PRESS_LEFT;
				break;
			case BGMT_PRESS_UP:
				last_arrow_faked = BGMT_PRESS_DOWN;
				break;
			case BGMT_PRESS_DOWN:
				last_arrow_faked = BGMT_PRESS_UP;
				break;
			#ifdef BGMT_PRESS_UP_LEFT
			case BGMT_PRESS_UP_LEFT:
				last_arrow_faked = BGMT_PRESS_DOWN_RIGHT;
				break;
			case BGMT_PRESS_DOWN_RIGHT:
				last_arrow_faked = BGMT_PRESS_UP_LEFT;
				break;
			case BGMT_PRESS_UP_RIGHT:
				last_arrow_faked = BGMT_PRESS_DOWN_LEFT;
				break;
			case BGMT_PRESS_DOWN_LEFT:
				last_arrow_faked = BGMT_PRESS_UP_RIGHT;
				break;
			#endif
			default:
				return 1;
		}
		fake_simple_button(last_arrow_faked); return 0;
	}

	return 1;
}

void upside_down_step()
{
	extern int menu_upside_down;
	if (menu_upside_down)
	{
		if (!gui_menu_shown())
		{
			bmp_draw_to_idle(1);
			canon_gui_disable_front_buffer();
			BMP_LOCK(
				bmp_flip(bmp_vram_real(), bmp_vram_idle());
			)
		}
		//~ msleep(100);
	}
}

void screenshot_start();
void take_screenshot();

struct menu_entry expo_tweak_menus[] = {
	{
		.name = "LVGain (NightVision)", 
		.priv = &display_gain,
		.select = display_gain_toggle_forward, 
		.select_reverse = display_gain_toggle_reverse,
		.select_auto = display_gain_reset,
		.display = display_gain_print, 
		.help = "Boosts LV digital display gain (Photo, Movie w.AutoISO)",
	},
	{
		.name = "Exposure Simulation",
		.priv = &expsim_setting,
		.select = expsim_toggle,
		.select_reverse = expsim_toggle_reverse,
		.display = expsim_display,
		.help = "ExpSim: LCD image reflects exposure settings (ISO+Tv+Av).",
	},
};

static struct menu_entry display_menus[] = {
	{
		.name = "Upside-Down",
		.priv = &menu_upside_down,
		.display = menu_upside_down_print,
		.select = menu_binary_toggle,
		.help = "Displays all graphics upside-down and flips arrow keys.",
	},
#if defined(CONFIG_60D) || defined(CONFIG_600D)
	{
		.name = "Auto Mirroring",
		.priv = &display_dont_mirror,
		.display = display_dont_mirror_display, 
		.select = menu_binary_toggle,
		.help = "Prevents display mirroring, which may reverse ML texts."
	},
#endif
#if defined(CONFIG_60D) || defined(CONFIG_600D)
	{
		.priv		= "Display: Normal/Reverse/Mirror",
		.select		= NormalDisplay,
		.select_reverse = ReverseDisplay,
		.select_auto = MirrorDisplay,
		.display	= menu_print,
		.help = "Display image: Normal [SET] / Reverse [PLAY] / Mirror [Q]"
	},
#endif
	{
		.name = "Screenshot (10 s)",
		.priv		= "Screenshot (10 s)",
		.select		= screenshot_start,
		.select_auto = take_screenshot,
		.display	= menu_print,
		.help = "Take a screenshot after 10 seconds [SET] or right now [Q].",
	},
#ifdef CONFIG_KILL_FLICKER
	{
		.name		= "Kill Canon GUI",
		.priv		= &kill_canon_gui_mode,
		.select		= menu_ternary_toggle,
		.select_reverse = menu_ternary_toggle_reverse,
		.display	= kill_canon_gui_print,
		.help = "Workarounds for disabling Canon graphics elements."
	},
#endif
	#ifdef CONFIG_60D
	{
		.name = "DispOFF in PhotoMode",
		.priv = &display_off_by_halfshutter_enabled,
		.display = display_off_by_halfshutter_print, 
		.select = menu_binary_toggle,
		.help = "Outside LV, turn off display with long half-shutter press."
	},
	#endif
};

struct menu_entry play_menus[] = {
	{
		.name = "SET+MainDial (PLAY)",
		.priv = &play_set_wheel_action, 
		.select = menu_quaternary_toggle, 
		.display = play_set_wheel_display,
		.help = "What to do when you hold SET and turn MainDial (Wheel)",
		.essential = FOR_PLAYBACK,
	},
	{
		.name = "Cropmarks (PLAY)",
		.priv = &cropmarks_play, 
		.select = menu_binary_toggle, 
		.display = cropmarks_play_display,
		.help = "Show cropmarks in PLAY mode",
		.essential = FOR_PLAYBACK,
	},
	{
		.name = "After taking a photo",
		.priv = &quick_review_allow_zoom, 
		.select = menu_binary_toggle, 
		.display = qrplay_display,
		.help = "When you set \"ImageReview: Hold\", it will go to Play mode.",
		.essential = FOR_PLAYBACK,
	},
	{
		.name = "Zoom in PLAY mode",
		.priv = &quickzoom, 
		.select = menu_ternary_toggle, 
		.display = quickzoom_display,
		.help = "Faster zoom in Play mode, for pixel peeping :)",
		.essential = FOR_PLAYBACK,
	},
#if defined(CONFIG_60D) || defined(CONFIG_600D)
	{
		.name = "LV button (PLAY)",
		.priv = &play_lv_action, 
		.select = menu_binary_toggle, 
		.display = play_lv_display,
		.help = "You may use the LiveView button to protect images quickly.",
		.essential = FOR_PLAYBACK,
	},
#endif
	{
		.name = "Quick Erase",
		.priv = &quick_delete, 
		.select = menu_binary_toggle, 
		.display = quick_delete_print,
		.help = "Delete files quickly with SET+Erase (be careful!!!)",
		.essential = FOR_PLAYBACK,
	},
};

static void tweak_init()
{
	menu_add( "Tweaks", tweak_menus, COUNT(tweak_menus) );
	menu_add( "Play", play_menus, COUNT(play_menus) );
	menu_add( "Display", display_menus, COUNT(display_menus) );
}

INIT_FUNC(__FILE__, tweak_init);
