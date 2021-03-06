#ifndef GUI_TOPLEVEL_H
#define GUI_TOPLEVEL_H

#include "gui.h"

/*----------------------------------------------------------------------------------------------*/
class GUI_Toplevel: public iGUI_Toplevel
{

	private:

		/* Add all the "subwindows" here */
		int count;
		GUI_Main		*wMain;
		GUI_Channel		*wChannel;
		GUI_Pseudo		*wPseudo;
		GUI_Commands	*wCommands;
		GUI_Almanac		*wAlmanac;
		GUI_Ephemeris	*wEphemeris;
		GUI_Select		*wSelect;
		GUI_Acquisition	*wAcquisition;
		class GUI_Serial*pSerial;

		wxTimer 		*timer;
		wxString		status_str;

		/* GPS-SDR exec variables */
		wxInputStream* 	gps_in;
		wxOutputStream* gps_out;
		wxProcess*		gps_proc;
		int				gps_pid;
		int				gps_active;

		/* GPS-USRP exec variables */
		wxInputStream* 	usrp_in;
		wxOutputStream* usrp_out;
		wxProcess*		usrp_proc;
		int				usrp_pid;
		int				usrp_active;

		int				bytes_pres;
		int				bytes_prev;
		int				bytes_sec;

		Message_Struct	messages;						//!< Hold all the messages

		float 			kB_sec;
		int				last_tic;						//!< Only update when new info is available

	public:

		GUI_Toplevel();
		~GUI_Toplevel();


		void onTimer(wxTimerEvent& evt);
		void onClose(wxCloseEvent& evt);
		void onQuit(wxCommandEvent& event);
		void onAbout(wxCommandEvent& event);
		void onGPSStart(wxCommandEvent& event);
		void onGPSStop(wxCommandEvent& event);
		void onUSRPStart(wxCommandEvent& event);
		void onUSRPStop(wxCommandEvent& event);
		void onLogConfig(wxCommandEvent& event);
		void onLogStart(wxCommandEvent& event);
		void onLogStop(wxCommandEvent& event);
		void onLogClear(wxCommandEvent& event);
		void onNamedPipe(wxCommandEvent& event);
		void onSerialPort(wxCommandEvent& event);

		void onMain(wxCommandEvent& event);
		void onChannel(wxCommandEvent& event);
		void onPseudo(wxCommandEvent& event);
		void onCommands(wxCommandEvent& event);
		void onEphemeris(wxCommandEvent& event);
		void onAlmanac(wxCommandEvent& event);
		void onSpeed(wxCommandEvent& event);
		void onSelect(wxCommandEvent& event);
		void onAcquisition(wxCommandEvent& event);

	    void paintEvent(wxPaintEvent& evt);
	    void paintNow();

	    void render(wxDC& dc);
	    void renderFIFO();
	    void renderRS422();
	    void renderTask();

		DECLARE_EVENT_TABLE()

};
/*----------------------------------------------------------------------------------------------*/

#endif
