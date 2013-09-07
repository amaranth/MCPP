static const String mods_dir("mods");
static const String startup_prepend("Startup: ");


inline void Server::server_startup () {

	//	Get a data provider
	data=DataProvider::GetDataProvider();

	//	Log callback for components
	MCPP::LogType log(
		[=] (const String & message, Service::LogType type) -> void {	WriteLog(message,type);	}
	);
	
	//	Load mods
	mods.Construct(
		Path::Combine(
			Path::GetPath(
				File::GetCurrentExecutableFileName()
			),
			mods_dir
		),
		[this] (const String & message, Service::LogType type) {
		
			String log(startup_prepend);
			log << message;
			
			WriteLog(log,type);
		
		}
	);
	mods->Load();

	//	Clear clients in case an existing
	//	instance is being recycled
	Clients.Clear();
	//	Clear routes for same reason
	Router.Clear();
	
	//	Grab settings
	
	//	Maximum number of bytes to buffer
	Nullable<String> max_bytes_str(data->GetSetting(max_bytes_setting));
	if (
		max_bytes_str.IsNull() ||
		!max_bytes_str->ToInteger(&MaximumBytes)
	) MaximumBytes=default_max_bytes;
	
	//	Maximum number of players
	Nullable<String> max_players_str(data->GetSetting(max_players_setting));
	if (
		max_players_str.IsNull() ||
		!max_players_str->ToInteger(&MaximumPlayers)
	) MaximumPlayers=default_max_players;

	//	Initialize a thread pool
	
	//	Attempt to grab number of threads
	//	from the data source
	Nullable<String> num_threads_str(data->GetSetting(num_threads_setting));
	
	Word num_threads;
	if (
		num_threads_str.IsNull() ||
		!num_threads_str->ToInteger(&num_threads) ||
		(num_threads==0)
	) num_threads=default_num_threads;
	
	//	Panic callback
	PanicType panic([=] () -> void {	Panic();	});
	
	//	Fire up the thread pool
	pool.Construct(num_threads,panic);
	
	//	Install mods
	OnInstall(true);
	mods->Install();
	OnInstall(false);
	
	//	Get binds
	Nullable<String> binds_str(data->GetSetting(binds_setting));
	
	Vector<Endpoint> binds;
	
	//	Parse binds
	if (!binds_str.IsNull()) {
	
		//	Binds are separated by a semi-colon
		Vector<String> split(Regex(";").Split(*binds_str));
		
		for (String & str : split) {
		
			bool set_port=false;
			bool set_ip=false;
		
			//	Attempt to extract port number
			RegexMatch port_match=Regex(
				"(?<!\\:)\\:\\s*(\\d+)\\s*$",
				RegexOptions().SetRightToLeft()
			).Match(str);
			
			UInt16 port_no;
			
			if (
				port_match.Success() &&
				port_match[1].Value().ToInteger(&port_no)
			) set_port=true;
			else port_no=default_port;
			
			IPAddress ip(IPAddress::Any());
			
			//	Attempt to extract IP address
			RegexMatch ip_match=Regex(
				"^(?:[^\\:]|\\:(?=\\:))+"
			).Match(str);
			
			if (ip_match.Success()) {
			
				try {
				
					IPAddress extracted_ip(ip_match.Value().Trim());
					
					ip=extracted_ip;
					
					set_ip=true;
				
				} catch (...) {	}
			
			}
			
			//	Did we actually set either of
			//	the values?
			if (set_port || set_ip) {
			
				//	Add bind
				binds.EmplaceBack(ip,port_no);
			
			} else {
			
				//	Log that we couldn't parse this bind
				WriteLog(
					String::Format(
						couldnt_parse_bind,
						str
					),
					Service::LogType::Warning
				);
				
			}
		
		}
	
	//	No binds
	} else {
	
		no_binds:
		
		//	IPv4 all, default port
		binds.EmplaceBack(IPAddress::Any(),default_port);
		//	IPv6 all, default port
		binds.EmplaceBack(IPAddress::Any(true),default_port);
	
	}
	
	//	If no binds
	if (binds.Count()==0) goto no_binds;
	
	//	Log binds
	String binds_desc;
	for (Word i=0;i<binds.Count();++i) {
	
		if (i!=0) binds_desc << list_separator;
		
		binds_desc << String::Format(
			endpoint_template,
			binds[i].IP(),
			binds[i].Port()
		);
	
	}
	
	WriteLog(
		String::Format(
			binding_to,
			binds_desc
		),
		Service::LogType::Information
	);
	
	//	Try and fire up the connection
	//	manager
	connections.Construct(
		binds,
		OnAccept,
		[=] (SmartPointer<Connection> conn) {
		
			//	Save IP and port number
			IPAddress ip=conn->IP();
			UInt16 port=conn->Port();
		
			try {
				
				//	Create client object
				auto client=SmartPointer<Client>::Make(
					std::move(
						conn
					)
				);
				
				//	Add to the client list
				Clients.Add(client);
				
				//	Fire event handler
				OnConnect(std::move(client));
				
			} catch (...) {
			
				//	Panic on error
				Panic();
				
				throw;
			
			}
			
			try {
				
				//	Log
				auto clients=Clients.Count();
				WriteLog(
					String::Format(
						connected,
						ip,
						port,
						clients,
						(clients==1) ? "is" : "are",
						(clients==1) ? "" : "s"
					),
					Service::LogType::Information
				);
				
			} catch (...) {	}
		
		},
		[=] (SmartPointer<Connection> conn, const String & reason) {
		
			try {
			
				//	Look up the client
				auto client=Clients[*conn];
				
				//	Remove the client from the
				//	list
				Clients.Remove(*conn);
				
				//	Fire event handler
				OnDisconnect(client,reason);
				
			} catch (...) {
			
				//	Panic on error
				Panic();
				
				throw;
			
			}
			
			try {
				
				//	Log
				String disconnect_template;
				
				//	If there's no reason, choose
				//	the template with no reason
				if (reason.Size()==0) {
				
					disconnect_template=disconnected;
				
				} else {
				
					//	Fill the reason into the template
					disconnect_template=String::Format(
						disconnected_with_reason,
						reason
					);
				
				}
				
				auto clients=Clients.Count();
				WriteLog(
					String::Format(
						disconnect_template,
						conn->IP(),
						conn->Port(),
						clients,
						(clients==1) ? "is" : "are",
						(clients==1) ? "" : "s"
					),
					Service::LogType::Information
				);
				
			} catch (...) {	}
		
		},
		OnReceive,
		log,
		panic,
		*pool
	);
	
}
