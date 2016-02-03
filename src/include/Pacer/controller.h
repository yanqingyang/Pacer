/****************************************************************************
 * Copyright 2014 Samuel Zapolsky
 * This library is distributed under the terms of the Apache V2.0
 * License (obtainable from http://www.apache.org/licenses/LICENSE-2.0).
 ****************************************************************************/
#ifndef CONTROL_H
#define CONTROL_H

#include <Pacer/robot.h>
#include <Moby/XMLTree.h>
#include <Moby/XMLReader.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

namespace Pacer{
  
  class Controller;
  
  typedef void (*update_t)(const boost::shared_ptr<Controller>&, double);
  
  const int
  NON_REALTIME = -1,
  HIGHEST_PRIORITY = 0,
  LOWEST_PRIORITY = 10;
  
  class Controller : public Robot, public boost::enable_shared_from_this<Controller>
  {
  public:
    boost::shared_ptr<Controller> ptr()
    {
      return shared_from_this();
    }
    /**
     * @brief Controller constructor
     * @see Robot()
     */
    Controller();
    virtual ~Controller();
    void init(){
      // ================= INIT LOGGING ==========================
      FILELog::ReportingLevel() = FILELog::FromString("DEBUG1");
#ifdef LOG_TO_FILE
      FILE * pFile;
      static int pid = getpid();
      char buffer[9];
      sprintf(buffer,"%06d",pid);
      static std::string name("out-"+std::string(buffer)+".log");
      pFile = fopen (name.c_str(),"w");
      fprintf(pFile, "INITED LOGGER\n");
      fflush(pFile);
      fclose (pFile);
#endif
      // ================= LOAD VARS ==========================
      PARAMS_FILE = std::string("vars.xml");
      load_variables(PARAMS_FILE,"");
      // ================= SETUP LOGGING ==========================
      const std::string LOG_TYPE = get_data<std::string>("logging");
      OUT_LOG(logDEBUG1) << "Log Type : " << LOG_TYPE;
      FILELog::ReportingLevel() =
      FILELog::FromString( (!LOG_TYPE.empty() ) ? LOG_TYPE : "INFO");
      // ================= INIT ROBOT ==========================
      init_robot();
      // After Robot loads, load plugins
    }
  private:
    std::string PARAMS_FILE;
    void load_variables(std::string xml_file, std::string root);
    void process_tag(std::string tag,boost::shared_ptr<const Moby::XMLTree> node);
    
    // call Pacer at time t
  public:
    void control(double t);
    
    void add_plugin_update(int priority,const std::string& name,update_t f){
      // Fix priority
      if(priority > LOWEST_PRIORITY || priority < NON_REALTIME){
        OUT_LOG(logERROR) << "Set priorities to range [-1,0.."<<LOWEST_PRIORITY<<"], -1 is for non-realtime processes (will only return data when complete)";
        if(priority < NON_REALTIME)
          priority = NON_REALTIME;
        else if(priority > LOWEST_PRIORITY)
          priority = LOWEST_PRIORITY;
      }
      
      // Check if this function already has an updater
      if(_name_priority_map.find(name) != _name_priority_map.end())
        remove_plugin_update(name);
      
      // add plugin back in at new priority
      _update_priority_map[priority][name] = f;
      _name_priority_map[name] = priority;
    }
    
    void add_plugin_deconstructor(const std::string& name,update_t f){
      _plugin_deconstruct_map[name] = f;
    }
    
    
    void close_plugin(const std::string& name){
      OUT_LOG(logINFO) << "maked plugin '" << name << "' for closure.";
      plugins_to_close.push_back(name);
    }
    void open_plugin(const std::string& name){
      OUT_LOG(logINFO) << "maked plugin '" << name << "' for open.";
      plugins_to_open.push_back(name);
    }
    
  private:
    typedef void (*init_t)(const boost::shared_ptr<Controller>, const char*);
    static std::map<std::string, void*> handles;
    typedef std::map<std::string , update_t> name_update_t;
    
    std::map<int , name_update_t> _update_priority_map;
    name_update_t _plugin_deconstruct_map;
    std::map< std::string , int > _name_priority_map;
    std::vector<std::string> plugins_to_open, plugins_to_close;
    
    bool reload_plugin(const std::string& name){
      remove_plugin(name);
      return init_plugin(name);
    }
    
    bool init_plugin(const std::string& name);
    bool remove_plugin(const std::string& plugin_name);
    void remove_plugin_update(const std::string& name){
      (*_plugin_deconstruct_map[name])(this->ptr(),0);
      
      //delete _update_priority_map.at(_name_priority_map.at(name)).at(name)->second;
      _update_priority_map[_name_priority_map[name]].erase(name);
      _name_priority_map.erase(name);
      _plugin_deconstruct_map.erase(name);
      
    }
    
    bool close_all_plugins();
    bool init_all_plugins();
    
    bool update_plugins(double t){
    #ifdef USE_PLUGINS
      if(t == 0){
        if(!init_all_plugins())
          throw std::runtime_error("One of the plugins failed to load");
      }
    #endif
      OUT_LOG(logINFO) << ">> update_plugins()";
      //name_update_t& non_realtime_map = _update_priority_map[NON_REALTIME];
      //if(!_update_priority_map[i].empty())
      //BOOST_FOREACH( const name_update_t::value_type& update, non_realtime_map)
      //{
      //static std::map<std::string,std::thread> threads = std::map<std::string,std::thread>();
      //if(threads.find(update.first) != threads.end());
      //OUT_LOG(logDEBUG1) << ">> " << update.first;
      //threads[update.first] = (*(update.second)),this->ptr(),t);
      //OUT_LOG(logDEBUG1) << "<< " << update.first;
      //}
      
      // remove the plugins that have been marked for closure
      if(!plugins_to_close.empty()){
        BOOST_FOREACH( const std::string& name, plugins_to_close){
          OUT_LOG(logINFO) << "CLOSING " << name;
          remove_plugin(name);
        }
        plugins_to_close.clear();
      }
      
      // add the plugins that have been marked for addition
      if(!plugins_to_open.empty()){
        BOOST_FOREACH( const std::string& name, plugins_to_open){
          OUT_LOG(logINFO) << "OPENING " << name;
          init_plugin(name);
        }
        plugins_to_open.clear();
      }
      
#ifdef LOG_TO_FILE
      for(int i = HIGHEST_PRIORITY;i<=LOWEST_PRIORITY;i++){
        if(!_update_priority_map[i].empty()){
          OUT_LOG(logINFO) << "PRIORITY: " << i ;
          OUT_LOG(logINFO) << "\t" << get_map_keys(_update_priority_map[i]);
        }
      }
#endif
      
      // Update plugins in priority queue
      for(int i = HIGHEST_PRIORITY;i<=LOWEST_PRIORITY;i++){
        if(!_update_priority_map[i].empty()){ // SRZ: do I need this line?
          BOOST_FOREACH( const name_update_t::value_type& update, _update_priority_map[i]){
            OUT_LOG(logINFO) << ">> " << update.first;
            (*(update.second))(this->ptr(),t);
            OUT_LOG(logINFO) << "<< " << update.first;
          }
        }
      }
      
      OUT_LOG(logINFO) << "<< update_plugins()";
      return true;
    }
  };
}
#endif // CONTROL_H