/*
 *
 *  Copyright (c) 2014-2015
 *  name : Francis Banyikwa
 *  email: mhogomchungu@gmail.com
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "plugins.h"
#include "siritask.h"
#include "mountinfo.h"
#include "winfsp.h"

#include <QDir>
#include <QString>
#include <QDebug>
#include <QFile>

using cs = siritask::status ;

static bool _create_folder( const QString& m )
{
	if( utility::platformIsWindows() ){

		return true ;
	}else{
		if( utility::pathExists( m ) ){

			return utility::reUseMountPoint() ;
		}else{
			return utility::createFolder( m ) ;
		}
	}
}

static QString _makePath( const QString& e )
{
	return utility::Task::makePath( e ) ;
}

template< typename T >
static bool _ecryptfs( const T& e )
{
	return utility::equalsAtleastOne( e,"ecryptfs","ecryptfs-simple" ) ;
}

static bool _ecryptfs_illegal_path( const siritask::options& opts )
{
	if( _ecryptfs( opts.type ) && utility::useSiriPolkit() ){

		return opts.cipherFolder.contains( " " ) || opts.plainFolder.contains( " " ) ;
	}else{
		return false ;
	}
}

template< typename ... T >
static bool _deleteFolders( const T& ... m )
{
	bool s = false ;

	QDir e ;

	for( const auto& it : { m ... } ){

		s = e.rmdir( it ) ;
	}

	return s ;
}

static void _run_command_on_mount( const siritask::options& opt,const QString& app )
{
	auto exe = utility::runCommandOnMount() ;

	if( !exe.isEmpty() ){

		auto a = _makePath( opt.cipherFolder ) ;
		auto b = _makePath( opt.plainFolder ) ;

		exe = QString( "%1 %2 %3 %4" ).arg( exe,a,b,app ) ;

		QProcess e ;

		e.setProcessEnvironment( utility::systemEnvironment() ) ;

		e.startDetached( exe ) ;
	}
}

bool siritask::deleteMountFolder( const QString& m )
{
	if( utility::reUseMountPoint() ){

		return false ;
	}else{
		if( utility::platformIsWindows() ){

			return true ;
		}else{
			return _deleteFolders( m ) ;
		}
	}
}

static QString _wrap_su( const QString& s )
{
	auto su = utility::executableFullPath( "su" ) ;

	if( su.isEmpty() ){

		return s ;
	}else{
		return QString( "%1 - -c \"%2\"" ).arg( su,QString( s ).replace( "\"","'" ) ) ;
	}
}

static utility::result< utility::Task > _unmount_volume( const QString& exe,
							 const QString& mountPoint,
							 bool usePolkit )
{
	auto e = utility::preUnMountCommand() ;

	int timeOut = 10000 ;

	if( e.isEmpty() ){

		return utility::Task::run( exe,timeOut,usePolkit ).get() ;
	}else{
		if( utility::Task::run( e + " " + mountPoint,timeOut,false ).get().success() ){

			return utility::Task::run( exe,timeOut,usePolkit ).get() ;
		}else{
			return {} ;
		}
	}
}

static bool _unmount_ecryptfs( const QString& cipherFolder,const QString& mountPoint,int maxCount )
{
	bool not_set = true ;

	auto cmd = [ & ](){

		auto exe = utility::executableFullPath( "ecryptfs-simple" ) ;

		auto s = exe + " -k " + cipherFolder ;

		if( utility::useSiriPolkit() ){

			return _wrap_su( s ) ;
		}else{
			return s ;
		}
	} ;

	for( int i = 0 ; i < maxCount ; i++ ){

		auto s = _unmount_volume( cmd(),mountPoint,true ) ;

		if( s && s.value().success() ){

			return true ;
		}else{
			if( not_set && s.value().stdError().contains( "error: failed to set gid" ) ){

				if( utility::enablePolkit( utility::background_thread::True ) ){

					not_set = false ;
				}else{
					return false ;
				}
			}else{
				utility::Task::waitForOneSecond() ;
			}
		}
	}

	return false ;
}

static bool _unmount_rest( const QString& mountPoint,int maxCount )
{
	if( utility::platformIsWindows() ){

		return SiriKali::Winfsp::FspLaunchStop( mountPoint ).success() ;
	}

	auto cmd = [ & ](){

		if( utility::platformIsOSX() ){

			return "umount " + mountPoint ;
		}else{
			return "fusermount -u " + mountPoint ;
		}
	}() ;

	for( int i = 0 ; i < maxCount ; i++ ){

		auto s = _unmount_volume( cmd,mountPoint,false ) ;

		if( s && s.value().success() ){

			return true ;
		}else{
			utility::Task::waitForOneSecond() ;
		}
	}

	return false ;
}

Task::future< bool >& siritask::encryptedFolderUnMount( const QString& cipherFolder,
							const QString& mountPoint,
							const QString& fileSystem )
{
	return Task::run( [ = ](){

		const int max_count = 5 ;

		if( _ecryptfs( fileSystem ) ){

			auto a = _makePath( cipherFolder ) ;
			auto b = _makePath( mountPoint ) ;

			return _unmount_ecryptfs( a,b,max_count ) ;
		}else{
			return _unmount_rest( _makePath( mountPoint ),max_count ) ;
		}
	} ) ;
}

struct cmdArgsList
{
	const QString& exe ;
	const siritask::options& opt ;
	const QString& configFilePath ;
	const QString& separator ;
	const QString& idleTimeOut ;
	const QString& cipherFolder ;
	const QString& mountPoint ;
	const bool create ;
};

static QString _ecryptfs( const cmdArgsList& args )
{
	auto mode = [ & ](){

		if( args.opt.ro ){

			return "--readonly" ;
		}else{
			return "" ;
		}
	}() ;

	auto e = QString( "%1 %2 %3 -a %4 %5 %6" ) ;

	auto s = [ & ]{

		if( args.create ){

			return e.arg( args.exe,
				      args.opt.createOptions,
				      mode,
				      args.configFilePath,
				      args.cipherFolder,
				      args.mountPoint ) ;
		}else{
			return e.arg( args.exe,
				      "-o key=passphrase",
				      mode,
				      args.configFilePath,
				      args.cipherFolder,
				      args.mountPoint ) ;
		}
	}() ;

	if( args.opt.mountOptions.isEmpty() ){

		if( utility::useSiriPolkit() ){

			return _wrap_su( s ) ;
		}else{
			return s ;
		}
	}else{
		if( utility::useSiriPolkit() ){

			return _wrap_su( s + " -o " + args.opt.mountOptions ) ;
		}else{
			return s + " -o " + args.opt.mountOptions ;
		}
	}
}

static QString _mountOptions( const cmdArgsList& args,
			      const QString& mountOptions,
			      const QString& type,
			      const QString& subtype = QString() )
{
	QString e = [ & ](){

		if( args.opt.ro ){

			return " -o ro,fsname=%1@%2%3" ;
		}else{
			return " -o rw,fsname=%1@%2%3" ;
		}
	}() ;

	if( mountOptions.isEmpty() ){

		return e.arg( type,args.cipherFolder,subtype ) ;
	}else{
		return e.arg( type,args.cipherFolder,subtype ) + "," + mountOptions ;
	}
}

static QString _gocryptfs( const cmdArgsList& args )
{
	if( args.create ){

		QString e = "%1 --init -q %2 %3 %4" ;
		return e.arg( args.exe,
			      args.opt.createOptions,
			      args.configFilePath,
			      args.cipherFolder ) ;
	}else{
		QString e = "%1 -q %2 %3 %4 %5" ;
		return e.arg( args.exe,
			      args.configFilePath,
			      args.cipherFolder,
			      args.mountPoint,
			      _mountOptions( args,args.opt.mountOptions,"gocryptfs" ) ) ;
	}
}

static QString _securefs( const cmdArgsList& args )
{
	if( args.create ){

		QString e = "%1 create %2 %3 %4" ;
		return e.arg( args.exe,
			      args.opt.createOptions,
			      args.configFilePath,
			      args.cipherFolder ) ;
	}else{
		QString exe = [ & ](){

			if( utility::platformIsWindows() ){

				return "%1 mount %2 %3 %4 %5" ;
			}else{
				return "%1 mount -b %2 %3 %4 %5" ;
			}
		}() ;

		return exe.arg( args.exe,
				args.configFilePath,
				args.cipherFolder,
				args.mountPoint,
				_mountOptions( args,args.opt.mountOptions,
					       "securefs",",subtype=securefs" ) ) ;
	}
}

static QString _cryfs( const cmdArgsList& args )
{
	auto e = QString( "%1 %2 %3 %4 %5 %6 %7" ) ;

	auto mountOptions = args.opt.mountOptions ;

	auto exe = [ & ]()->QString{

		if( args.create ){

			return args.exe + " " + args.opt.createOptions ;
		}else{
			if( mountOptions.contains( " --allow-filesystem-upgrade" ) ){

				mountOptions.replace( " --allow-filesystem-upgrade","" ) ;

				return args.exe + " --allow-filesystem-upgrade" ;
			}else{
				return args.exe ;
			}
		}
	}() ;

	return e.arg( exe,
		      args.cipherFolder,
		      args.mountPoint,
		      args.idleTimeOut,
		      args.configFilePath,
		      args.separator,
		      _mountOptions( args,mountOptions,"cryfs",",subtype=cryfs" ) ) ;
}

static QString _encfs( const cmdArgsList& args )
{
	auto e = [](){

		if( utility::platformIsWindows() ){

			return QString( "%1 -f %2 %3 %4 %5 %6 %7" ) ;
		}else{
			return QString( "%1 %2 %3 %4 %5 %6 %7" ) ;
		}
	}() ;

	auto mountOptions = _mountOptions( args,args.opt.mountOptions,"encfs",",subtype=encfs" ) ;

	if( utility::platformIsOSX() ){

		mountOptions += ",volname=" + utility::split( args.opt.plainFolder,'/' ).last() ;
	}

	return e.arg( args.exe,
		      args.cipherFolder,
		      args.mountPoint,
		      args.idleTimeOut,
		      args.configFilePath,
		      args.separator,
		      mountOptions ) ;
}

static QString _sshfs( const cmdArgsList& args )
{
	QString s = [](){

		if( utility::platformIsWindows() ){

			return "%1 -f %2 %3 %4" ;
		}else{
			return "%1 %2 %3 %4" ;
		}
	}() ;

	auto mountOptions = args.opt.mountOptions ;

	if( !args.opt.key.isEmpty() ){

		if( mountOptions.isEmpty() ){

			mountOptions = "password_stdin,StrictHostKeyChecking=no" ;
		}else{
			mountOptions += ",password_stdin,StrictHostKeyChecking=no" ;
		}
	}

	return s.arg( args.exe,
		      args.opt.cipherFolder,
		      args.opt.plainFolder,
		      _mountOptions( args,mountOptions,"sshfs",",subtype=sshfs" ) ) ;
}

static QString _args( const QString& exe,const siritask::options& opt,
		      const QString& configFilePath,
		      bool create )
{
	auto cipherFolder = _makePath( opt.cipherFolder ) ;

	auto mountPoint   = _makePath( opt.plainFolder ) ;

	auto idleTimeOut = [ & ](){

		if( !opt.idleTimeout.isEmpty() ){

			if( opt.type == "cryfs" ){

				return QString( "--unmount-idle %1" ).arg( opt.idleTimeout ) ;

			}else if( opt.type == "encfs" ){

				return QString( "--idle=%1" ).arg( opt.idleTimeout ) ;
			}
		}

		return QString() ;
	}() ;

	auto separator = [ & ](){

		if( opt.type == "cryfs" ){

			/*
			 * declaring this variable as static to force this function to be called only
			 * once.
			 */
			static auto m = utility::backendIsLessThan( "cryfs","0.10" ).get() ;

			if( m && m.value() ){

				return "--" ;
			}else{
				return "" ;
			}

		}else if( opt.type == "encfs" ){

			if( create ){

				return "-S --standard" ;
			}else{
				return "-S" ;
			}
		}else{
			return "" ;
		}
	}() ;

	auto configPath = [ & ](){

		if( opt.type.isOneOf( "cryfs","gocryptfs","securefs","ecryptfs" ) ){

			if( !configFilePath.isEmpty() ){

				return "--config " + _makePath( configFilePath ) ;
			}
		}

		return QString() ;
	}() ;

	cmdArgsList arguments{  exe,
				opt,
				configPath,
				separator,
				idleTimeOut,
				cipherFolder,
				mountPoint,
				create } ;

	if( opt.type == "gocryptfs" ){

		return _gocryptfs( arguments ) ;

	}else if( opt.type == "securefs" ){

		return _securefs( arguments ) ;

	}else if( _ecryptfs( opt.type ) ){

		return _ecryptfs( arguments ) ;

	}else if( opt.type == "cryfs" ){

		return _cryfs( arguments ) ;

	}else if( opt.type == "encfs" ){

		return _encfs( arguments ) ;

	}else if( opt.type == "sshfs" ){

		return _sshfs( arguments ) ;
	}else{
		/*
		 * We Should not get here
		 */
		return QString() ;
	}
}

enum class status_type{ exeName,exeNotFound } ;
static siritask::status _status( const siritask::volumeType& app,status_type s )
{
	if( s == status_type::exeNotFound ){

		if( app == "cryfs" ){

			return cs::cryfsNotFound ;

		}else if( app == "encfs" ){

			return cs::encfsNotFound ;

		}else if( app == "securefs" ){

			return cs::securefsNotFound ;

		}else if( _ecryptfs( app ) ){

			return cs::ecryptfs_simpleNotFound ;

		}else if( app == "gocryptfs" ){

			return cs::gocryptfsNotFound ;

		}else if( app =="sshfs" ){

			return cs::sshfsNotFound ;
		}else{
			return cs::unknown ;
		}
	}else{
		if( app == "cryfs" ){

			return cs::cryfs ;

		}else if( app == "encfs" ){

			return cs::encfs ;

		}else if( app == "securefs" ){

			return cs::securefs ;

		}else if( _ecryptfs( app ) ){

			return cs::ecryptfs ;

		}else if( app == "sshfs" ){

			return cs::sshfs ;

		}else if( app == "gocryptfs" ){

			return cs::gocryptfs ;
		}else{
			return cs::unknown ;
		}
	}
}

static siritask::cmdStatus _status( const utility::Task& r,siritask::status s )
{
	if( r.success() ){

		return siritask::cmdStatus( siritask::status::success,r.exitCode() ) ;
	}

	siritask::cmdStatus e( r.exitCode(),r.stdError().isEmpty() ? r.stdOut() : r.stdError() ) ;

	const auto msg = e.msg().toLower() ;

	/*
	 *
	 * When trying to figure out what error occured,check for status value
	 * if the backend supports them and fallback to parsing output strings
	 * if backend does not support error codes.
	 *
	 */

	if( s == siritask::status::ecryptfs ){

		if( msg.contains( "operation not permitted" ) ){

			e = siritask::status::ecrypfsBadExePermissions ;

		}else if( msg.contains( "error: mount failed" ) ){

			e = s ;
		}

	}else if( s == siritask::status::cryfs ){

		/*
		 * Error codes are here: https://github.com/cryfs/cryfs/blob/develop/src/cryfs/ErrorCodes.h
		 *
		 * Valid for cryfs > 0.9.8
		 */

		auto m = e.exitCode() ;

		if( m == 11 ){

			e = s ;

		}else if( m == 14 ){

			e = siritask::status::cryfsMigrateFileSystem ;
		}else{
			/*
			 * Falling back to parsing strings
			 */

			if( msg.contains( "password" ) ){

				e = s ;

			}else if( msg.contains( "this filesystem is for cryfs" ) &&
				  msg.contains( "it has to be migrated" ) ){

				e = siritask::status::cryfsMigrateFileSystem ;
			}
		}

	}else if( s == siritask::status::encfs ){

		if( msg.contains( "password" ) ){

			e = s ;

		}else if( msg.contains( "cygfuse: initialization failed: winfsp-x86.dll not found" ) ){

			e = cs::failedToLoadWinfsp ;
		}

	}else if( s == siritask::status::gocryptfs ){

		/*
		 * This error code was added in gocryptfs 1.2.1
		 */
		if( e.exitCode() == 12 ){

			e = s ;
		}else{
			if( msg.contains( "password" ) ){

				e = s ;
			}
		}

	}else if( s == siritask::status::securefs ){

		if( msg.contains( "password" ) ){

			e = s ;

		}else if( msg.contains( "securefs cannot load winfsp" ) ){

			e = cs::failedToLoadWinfsp ;
		}

	}else if( s == siritask::status::sshfs ){

		if( msg.contains( "password" ) ){

			e = s ;
		}
	}

	return e ;
}

static utility::Task _run_task( const QString& cmd,
				const QString& password,
				const siritask::options& opts,
				bool create,
				bool ecryptfs )
{
	if( utility::platformIsWindows() ){

		if( create ){

			return SiriKali::Winfsp::FspLaunchRun( cmd,password.toLatin1(),opts ) ;
		}else{
			return SiriKali::Winfsp::FspLaunchStart( cmd,password.toLatin1(),opts ) ;
		}
	}else{
		return utility::Task( cmd,20000,utility::systemEnvironment(),
				      password.toLatin1(),[](){},ecryptfs ) ;
	}
}

static siritask::cmdStatus _cmd( bool create,const siritask::options& opts,
		const QString& password,const QString& configFilePath )
{
	auto opt = opts ;

	const auto& app = opt.type ;

	auto exe = app.executableFullPath() ;

	if( exe.isEmpty() ){

		return _status( app,status_type::exeNotFound ) ;
	}else{
		exe = utility::Task::makePath( exe ) ;

		auto _run = [ & ]()->std::pair< QString,siritask::cmdStatus >{

			auto cmd = _args( exe,opt,configFilePath,create ) ;

			auto s = _run_task( cmd,password,opt,create,_ecryptfs( app ) ) ;

			return { cmd,_status( s,_status( app,status_type::exeName ) ) } ;
		} ;

		auto s = _run() ;

		const auto& e = s.second ;

		if( e == siritask::status::ecrypfsBadExePermissions ){

			if( utility::enablePolkit( utility::background_thread::True ) ){

				s = _run() ;
			}
		}

		return e ;
	}
}

static QString _configFilePath( const siritask::options& opt )
{
	if( opt.configFilePath.isEmpty() ){

		return QString() ;
	}else{
		return QDir().absoluteFilePath( opt.configFilePath ) ;
	}
}

static siritask::cmdStatus _encrypted_folder_mount( const siritask::options& opt,bool reUseMountPoint )
{
	auto _mount = [ reUseMountPoint ]( const QString& app,const siritask::options& copt,
			const QString& configFilePath )->siritask::cmdStatus{

		auto opt = copt ;

		opt.type = app ;

		if( _ecryptfs_illegal_path( opt ) ){

			return cs::ecryptfsIllegalPath ;
		}

		if( _create_folder( opt.plainFolder ) || reUseMountPoint ){

			auto e = _cmd( false,opt,opt.key,configFilePath ) ;

			if( e == cs::success ){

				_run_command_on_mount( opt,app ) ;
			}else{
				siritask::deleteMountFolder( opt.plainFolder ) ;
			}

			return e ;
		}else{
			return cs::failedToCreateMountPoint ;
		}
	} ;

	if( opt.cipherFolder.startsWith( "sshfs " ) ){

		auto opts = opt ;
		opts.cipherFolder = opts.cipherFolder.remove( 0,6 ) ; // 6 is the size of "sshfs "

		if( !opts.key.isEmpty() ){

			auto m = opts.key ;

			/*
			 * On my linux box, sshfs prompts six times when entered password is wrong before
			 * giving up, here, we simulate replaying the password 10 times hoping it will be
			 * enough for sshfs.
			 */
			for( int i = 0 ; i < 9 ; i++ ){

				opts.key += "\n" + m ;
			}
		}

		return _mount( "sshfs",opts,QString() ) ;
	}

	if( opt.configFilePath.isEmpty() ){

		if( utility::pathExists( opt.cipherFolder + "/cryfs.config" ) ){

			return _mount( "cryfs",opt,QString() ) ;

		}else if( utility::pathExists( opt.cipherFolder + "/gocryptfs.conf" ) ){

			return _mount( "gocryptfs",opt,QString() ) ;

		}else if( utility::pathExists( opt.cipherFolder + "/.securefs.json" ) ){

			return _mount( "securefs",opt,QString() ) ;

		}else if( utility::pathExists( opt.cipherFolder + "/.ecryptfs.config" ) ){

			return _mount( "ecryptfs",opt,opt.cipherFolder + "/.ecryptfs.config" ) ;
		}else{
			auto encfs6 = opt.cipherFolder + "/.encfs6.xml" ;
			auto encfs5 = opt.cipherFolder + "/.encfs5" ;
			auto encfs4 = opt.cipherFolder + "/.encfs4" ;

			if( utility::atLeastOnePathExists( encfs6,encfs5,encfs4 ) ){

				return _mount( "encfs",opt,QString() ) ;
			}else{
				return cs::unknown ;
			}
		}
	}else{
		auto _path_exist = []( QString e,const QString& m )->utility::result< QString >{

			e.remove( 0,m.size() ) ;

			if( utility::pathExists( e ) ){

				return e ;
			}else{
				return {} ;
			}
		} ;

		auto e = opt.configFilePath ;

		if( e.startsWith( "[[[gocryptfs]]]" ) ){

			auto m = _path_exist( e,"[[[gocryptfs]]]" ) ;

			if( m ){

				return _mount( "gocryptfs",opt,m.value() ) ;
			}

		}else if( e.startsWith( "[[[ecryptfs]]]" ) ){

			auto m = _path_exist( e,"[[[ecryptfs]]]" ) ;

			if( m ){

				return _mount( "ecryptfs",opt,m.value() ) ;
			}

		}else if( e.startsWith( "[[[cryfs]]]" ) ){

			auto m = _path_exist( e,"[[[cryfs]]]" ) ;

			if( m ){

				return _mount( "cryfs",opt,m.value() ) ;
			}

		}else if( e.startsWith( "[[[securefs]]]" ) ){

			auto m = _path_exist( e,"[[[securefs]]]" ) ;

			if( m ){

				return _mount( "securefs",opt,m.value() ) ;
			}

		}else if( e.startsWith( "[[[encfs]]]" ) ){

			auto m = _path_exist( e,"[[[encfs]]]" ) ;

			if( m ){

				return _mount( "encfs",opt,m.value() ) ;
			}

		}else if( utility::pathExists( e ) ){

			if( e.endsWith( "gocryptfs.conf" ) ){

				return _mount( "gocryptfs",opt,e ) ;

			}else if( e.endsWith( "securefs.json" ) ){

				return _mount( "securefs",opt,e ) ;

			}else if( e.endsWith( "ecryptfs.config" ) ){

				return _mount( "ecryptfs",opt,e ) ;

			}else if( e.endsWith( "cryfs.config" ) ){

				return _mount( "cryfs",opt,e ) ;

			}else if( utility::endsWithAtLeastOne( e,".encfs6.xml",".encfs5",".encfs4" ) ){

				return _mount( "encfs",opt,e ) ;
			}
		}
	}

	return cs::unknown ;
}

static siritask::cmdStatus _encrypted_folder_create( const siritask::options& opt )
{
	if( _ecryptfs_illegal_path( opt ) ){

		return cs::ecryptfsIllegalPath ;
	}

	if( _create_folder( opt.cipherFolder ) ){

		if( _create_folder( opt.plainFolder ) ){

			auto e = _cmd( true,opt,[ & ]()->QString{

				if( opt.type.isOneOf( "securefs","encfs" ) ){

					return opt.key + "\n" + opt.key ;
				}else{
					return opt.key ;
				}

			}(),[ & ](){

				auto e = _configFilePath( opt ) ;

				if( e.isEmpty() && opt.type == "ecryptfs" ){

					return opt.cipherFolder + "/.ecryptfs.config" ;
				}else{
					return e ;
				}
			}() ) ;

			if( e == cs::success ){

				if( opt.type.isOneOf( "gocryptfs","securefs" ) ){

					e = siritask::encryptedFolderMount( opt,true ).get() ;

					if( e != cs::success ){

						_deleteFolders( opt.cipherFolder,opt.plainFolder ) ;
					}
				}
			}else{
				_deleteFolders( opt.plainFolder,opt.cipherFolder ) ;
			}

			return e ;
		}else{
			_deleteFolders( opt.cipherFolder ) ;

			return cs::failedToCreateMountPoint ;
		}
	}else{
		return cs::failedToCreateMountPoint ;
	}
}

Task::future< siritask::cmdStatus >& siritask::encryptedFolderCreate( const siritask::options& opt )
{
	return Task::run( _encrypted_folder_create,opt ) ;
}

Task::future< siritask::cmdStatus >& siritask::encryptedFolderMount( const siritask::options& opt,
								     bool reUseMountPoint )
{
	return Task::run( _encrypted_folder_mount,opt,reUseMountPoint ) ;
}
