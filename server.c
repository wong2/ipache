#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <string.h>
#include <pthread.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXPENDING 5
#define BUFSIZE 2000

GtkWidget *vboxInScrollWindow, *window;

char webDir[30];
char requestHead[50], responseCode[30], requestMsg[500], responseMsg[500];
int fileSize;
int servSock;

struct Client{
	int socket;
	char name[INET_ADDRSTRLEN];
	in_port_t port;
};

struct ThreadArgs{
 	struct Client client;
};

struct server_thread_args{
	pthread_t tid;
	gint terminate;
};

struct server_thread_args *server_thread = NULL;

void exitWithSystemError(const char *msg)
{
	perror(msg);
	exit(1);
}

// Create an IPv4, stream based, tcp socket
int createTCPSocket()
{
	int sock;
	if((sock=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		exitWithSystemError("Can't create socket");
	return sock;
}


void socketListen2Port(int sock, in_port_t port, int maxpending)
{
	struct sockaddr_in servAddr;

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(port);

	if(bind(sock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
		exitWithSystemError("bind() failed");

	if(listen(sock, maxpending) < 0)
		exitWithSystemError("listen() failed");
}

struct Client acceptClient(int sock)
{
	struct Client client;
	struct sockaddr_in clntAddr;
	socklen_t clntAddrLen = sizeof(clntAddr);

	int clntSock = accept(sock, (struct sockaddr *)&clntAddr, &clntAddrLen);
	if(clntSock < 0)
		exitWithSystemError("accept() failed");
	client.socket = clntSock;

	if(inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, client.name, sizeof(client.name)) != NULL)
		client.port = ntohs(clntAddr.sin_port);
	return client;
}

void recvData(int sock, void *buffer, int bufferSize)
{
	if(recv(sock, buffer, bufferSize, 0) < 0)
		exitWithSystemError("recv() failed");
}

int getFileAddrFromReq(char *fileAddr, char *buffer)
{
	int i = 0;
	int j = 0;
	char tmp[50];

	while(isspace(buffer[i]))
		i++;
	while(buffer[i] != ' '){
		tmp[j++] = buffer[i++];
	}
	tmp[j] = '\0';
	if(strcmp(tmp, "GET") != 0){
		return -1;
	}
	j = 0;
	while(buffer[++i] != ' '){
		tmp[j++] = buffer[i];
    }
	tmp[j] = '\0';
	strcat(fileAddr, tmp);
	return 1;
}

long getFileSizeInByte(char *path)
{
	struct stat buf;
	stat(path, &buf);
	return buf.st_size;
}

int isFileExist(char *fileName)
{
	return !access(fileName, F_OK);
}

void tranTime2Str(time_t t, char *buffer)
{
	struct tm  *ts;
	char tmp[80];
	ts = gmtime(&t);
    strftime(tmp, sizeof(tmp), "%a, %d %b %Y %H:%M:%S GMT", ts);
	strcpy(buffer, tmp);
}

char *getContentType(char *fileAddr)
{
	int i, j;
	int addrLen = strlen(fileAddr);
	char c, suffixTmp[4], suffix[5];
	i = 1, j = 0;

	while((c=fileAddr[addrLen-i]) != '.'){
		suffixTmp[j++] = c;
		i++;
	}
	i = 0;
	while(j > 0){
		suffix[i++] = suffixTmp[--j];
	}
	suffix[i] = '\0';

	if(strcmp(suffix, "jpg") == 0)
		return "image/jpeg";
	else if(strcmp(suffix, "gif") == 0)
		return "image/gif";
	else if(strcmp(suffix, "png") == 0)
		return "image/png";
	else if(strcmp(suffix, "bmp") == 0)
		return "image/bmp";
	else if(strcmp(suffix, "html") == 0)
		return "text/html";
	else if(strcmp(suffix, "htm") == 0)
		return "text/html";
	else if(strcmp(suffix, "css") == 0)
		return "text/css";
	else if(strcmp(suffix, "js") == 0)
		return "application/x-javascript";
	else if(strcmp(suffix, "mp3") == 0)
		return "audio/mpeg";
	else
		return "text/plain";
}

void appendDataToWindow()
{
    GtkWidget *view, *vbox, *expander;
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    char expanderText[100];

    vbox = gtk_vbox_new(FALSE, 0);

    view = gtk_text_view_new();
    gtk_box_pack_start(GTK_BOX(vbox), view, TRUE, TRUE, 0);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_create_tag(buffer, "lmarg", "left_margin", 40, NULL);
    gtk_text_buffer_create_tag(buffer, "blue_fg", "foreground", "blue", NULL);
    gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);

    gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);

    gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, "Request Message:\n", -1, "blue_fg", "bold",  NULL);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, requestMsg, -1, "lmarg",  NULL);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, "Response Message:\n", -1, "blue_fg", "bold",  NULL);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, responseMsg, -1, "lmarg",  NULL);

    sprintf(expanderText, "%s    %s    %d", requestHead, responseCode, fileSize);
    expander = gtk_expander_new(expanderText);

    gtk_container_add(GTK_CONTAINER(expander), vbox);
    gtk_expander_set_expanded(GTK_EXPANDER(expander), FALSE);
    gtk_box_pack_start(GTK_BOX(vboxInScrollWindow), expander, FALSE, TRUE, 0);
    gtk_widget_show_all(window);

}

void sendFileToClient(int cSock, char *fileAddr)
{
	FILE *fp;
	struct stat buf;
	char *rspsMsg, buffer[80];
	long fsize, len;

	stat(fileAddr, &buf);
	fsize = buf.st_size;
	fileSize = fsize;
	rspsMsg = malloc(sizeof(char) * (fsize+200));
	tranTime2Str(buf.st_ctime, buffer);

	sprintf(responseCode, "200 OK");

	sprintf(rspsMsg, "HTTP/1.1 200 OK\nConnection: keep-alive\nDate: %s\nServer: Ipache\nContent-Type: %s\nContent-Length: %ld\n\n",
					 buffer, getContentType(fileAddr), fsize);
strcpy(responseMsg, rspsMsg);
//puts(rspsMsg);
	len = strlen(rspsMsg);
	fp = fopen(fileAddr, "rb");
	fread(rspsMsg+len, fsize, 1, fp);
	len = len + fsize;

    send(cSock, rspsMsg, len, 0);
	fclose(fp);
	free(rspsMsg);
}

int sendErrorCode(int cSock, char *msg)
{
	char rspsMsg[40];
	sprintf(rspsMsg, "HTTP/1.1 %s\n\n", msg);
	int len = strlen(rspsMsg);

	strcpy(responseMsg, rspsMsg);
	strcpy(responseCode, msg);
    fileSize = 0;

	ssize_t numBytesSent = send(cSock, rspsMsg, len, 0);

	if(numBytesSent == len)
		return 1;
	else
		return 0;
}

void HandleClient(struct Client client)
{
	char *buffer, *fileAddr;
	char end[] = {'\r', '\n', '\r', '\n'};
	int i, j, hasMetSpaceBefore = 0;

	buffer = malloc(sizeof(char) * BUFSIZE);
    fileAddr = malloc(sizeof(char) * 80);
    strcpy(fileAddr, webDir);

    recvData(client.socket, buffer, BUFSIZE);

	i = 0;
    while(!(buffer[i]==' ' && hasMetSpaceBefore)){
        if(buffer[i] == ' ')
            hasMetSpaceBefore = 1;
        requestHead[i] = buffer[i];
        i++;
    }
    requestHead[i] = '\0';
    i = j = 0;
    while(j<4){
        if(buffer[i++] == end[j])
            j++;
        else
            j = 0;
    }
    buffer[i] = '\0';
    strcpy(requestMsg, buffer);
    if(getFileAddrFromReq(fileAddr, buffer) < 0){
	    sendErrorCode(client.socket, "501 Not Implemented");
    }
    else{
        if(isFileExist(fileAddr))
	        sendFileToClient(client.socket, fileAddr);
        else
            sendErrorCode(client.socket, "404 Not Found");
    }
gdk_threads_enter();
	appendDataToWindow();
gdk_threads_leave();

    close(client.socket);
    free(buffer);
    free(fileAddr);
}

void *ThreadMain(void *threadArgs){
	pthread_detach(pthread_self());

	struct Client client = ((struct ThreadArgs*)threadArgs)->client;
	free(threadArgs);

	HandleClient(client);
	return NULL;
}

void  MessageBox(GtkWindow *parentWindow, char *messageValue)
{
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new (parentWindow, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", messageValue);
    gtk_window_set_title (GTK_WINDOW (dialog), "About");
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}


void *startServer(void *threadArgs)
{
    FILE *fp;
	ssize_t numBytesSent;
	in_port_t servPort = 8889;

	servSock = createTCPSocket();
	socketListen2Port(servSock, servPort, MAXPENDING);

	while(server_thread->terminate == 0){
		struct Client client = acceptClient(servSock);

		struct ThreadArgs *threadArgs = (struct ThreadArgs *)malloc(
			sizeof(struct ThreadArgs));

		if(threadArgs == NULL)
			exitWithSystemError("malloc() failed");

		threadArgs->client = client;
		pthread_t threadID;
		int returnValue = pthread_create(&threadID, NULL, ThreadMain,
			threadArgs);
		if(returnValue != 0){
			puts("pthread_create() failed\n");
			exit(1);
		}
	}
	free(server_thread);
    return NULL;
}

void startServerThread(GtkWidget *widget, gpointer data)
{
    server_thread = g_malloc(sizeof(struct server_thread_args));
    server_thread->terminate = 0;
    pthread_create(&(server_thread->tid), NULL, startServer, server_thread);
    gtk_widget_set_sensitive(data, FALSE);
}

void stopServer(GtkWidget *widget, gpointer data)
{
    server_thread->terminate = 1;
	if(data)
    	gtk_widget_set_sensitive(data, TRUE);
    close(servSock);
}

void settingServer(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
	dialog = gtk_file_chooser_dialog_new("Select the web directory",
									      GTK_WINDOW(data),
										  GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
										  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
										  GTK_STOCK_OK, GTK_RESPONSE_OK,
										  NULL);
	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK){
		char *filename = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog));
		MessageBox(GTK_WINDOW(data), filename);
	}
	gtk_widget_destroy(dialog);
}

void aboutServer(GtkWidget *widget, gpointer window)
{
    MessageBox(window, "This is a web server");
}

void quitServer(GtkWidget *widget, gpointer data)
{
	stopServer(widget, NULL);
	gtk_main_quit();
}

int main(int argc, char *argv[])
{
    GtkWidget *scrollWin, *vboxInWindow, *toolbar, *hseparator;
    GtkToolItem *startBtn, *stopBtn, *toolbarSeparator1, *settingBtn, *aboutBtn, *toolbarSeparator2, *closeBtn;
	FILE *fp;
	if(!(fp=fopen("ipache.conf", "r"))){
		puts("Can't open config file.\n");
		exit(1);
	}
	fscanf(fp, "%s", webDir);

    g_thread_init (NULL);
    gdk_threads_init ();

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);
    gtk_window_set_title(GTK_WINDOW(window), "Ipache Server");

    vboxInWindow = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), vboxInWindow);

    toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);

    startBtn = gtk_tool_button_new(gtk_image_new_from_file("start.png"), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), startBtn, -1);
    stopBtn = gtk_tool_button_new(gtk_image_new_from_file("stop.png"), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), stopBtn, -1);
    toolbarSeparator1 = gtk_separator_tool_item_new ();
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), toolbarSeparator1, -1);
    settingBtn = gtk_tool_button_new(gtk_image_new_from_file("setting.png"), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), settingBtn, -1);
    aboutBtn = gtk_tool_button_new(gtk_image_new_from_file("about.png"), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), aboutBtn, -1);
    toolbarSeparator2 = gtk_separator_tool_item_new ();
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), toolbarSeparator2, -1);
	closeBtn = gtk_tool_button_new(gtk_image_new_from_file("close.png"), NULL);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), closeBtn, -1);

    gtk_box_pack_start(GTK_BOX(vboxInWindow), toolbar, FALSE, TRUE, 5);
    hseparator = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vboxInWindow), hseparator, FALSE, TRUE, 10);
    scrollWin = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vboxInWindow), scrollWin, TRUE, TRUE, 5);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollWin),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    vboxInScrollWindow = gtk_vbox_new(FALSE, 10);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrollWin), vboxInScrollWindow);

    g_signal_connect(G_OBJECT(startBtn), "clicked", G_CALLBACK(startServerThread), startBtn);
    g_signal_connect(G_OBJECT(stopBtn), "clicked", G_CALLBACK(stopServer), startBtn);
    g_signal_connect(G_OBJECT(settingBtn), "clicked", G_CALLBACK(settingServer), window);
    g_signal_connect(G_OBJECT(aboutBtn), "clicked", G_CALLBACK(aboutServer), window);
	g_signal_connect(G_OBJECT(closeBtn), "clicked", G_CALLBACK(quitServer), NULL);
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(quitServer), NULL);

    gtk_widget_show_all(window);

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();

    return 0;
}
