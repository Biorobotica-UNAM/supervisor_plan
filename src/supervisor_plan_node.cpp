#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;

class SupervisorPlanNode : public rclcpp::Node {
public:
    SupervisorPlanNode() : Node("plan_manager_node") {
        RCLCPP_INFO(this->get_logger(), "Nodo plan_manager_node iniciado");

        // Suscriptor para recibir planes completos
        plan_sub_ = this->create_subscription<std_msgs::msg::String>(
            "/clips/results", 10,
            std::bind(&SupervisorPlanNode::planCallback, this, std::placeholders::_1)
        );

        // Servicio para que el robot solicite instrucciones
        service_ = this->create_service<std_srvs::srv::Trigger>(
            "/request_instruction",
            std::bind(&SupervisorPlanNode::handleInstructionRequest, this,
                     std::placeholders::_1, std::placeholders::_2));

        // Publicador para enviar instrucciones
        publisher_ = this->create_publisher<std_msgs::msg::String>("/instruction_msg", 10);

        // Inicializar estado del plan
        current_plan_index_ = -1;
        instruction_counter_ = 0;
        has_active_plan_ = false;
        
        RCLCPP_INFO(this->get_logger(), "Plan Manager inicializado");
    }

private:
    // Variables para almacenar el plan
    std::vector<std::string> current_plan_;
    int current_plan_index_;
    int instruction_counter_;
    bool has_active_plan_;
    
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr plan_sub_;
    rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr service_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;

    // --------------------------
    // CALLBACK DEL TOPICO CLIPS
    // --------------------------
    void planCallback(const std_msgs::msg::String::SharedPtr msg) {
        std::string full_plan = msg->data;
        if (full_plan.empty()) {
            RCLCPP_WARN(this->get_logger(), "Plan vacío recibido");
            return;
        }

        RCLCPP_INFO(this->get_logger(), "Plan recibido: %s", full_plan.c_str());

        // Limpiar plan anterior
        current_plan_.clear();
        
        std::vector<std::string> lines;
        boost::split(lines, full_plan, boost::is_any_of("\n"));

        for (const string& line : lines) {
            if (line.find("Paso") != string::npos) {
                // Es una línea de paso del plan
                vector<string> tokens;
                boost::algorithm::split(tokens, line, boost::algorithm::is_any_of(" "));
                
                if (tokens.size() >= 3) {
                    // Reconstruir la instrucción (omitir "Paso X:")
                    string instruction = "";
                    bool start_collecting = false;
                    
                    for (size_t i = 0; i < tokens.size(); i++) {                        
                        if (start_collecting) {
                            instruction += tokens[i];
                            if (i < tokens.size() - 1) instruction += " ";
                        }
                        // Comenzar a recolectar después del número de paso
                        if (i >= 1 && tokens[i].find(":") != string::npos) {
                            start_collecting = true;
                        }
                    }
                    
                    if (!instruction.empty()) {
                        current_plan_.push_back(instruction);
                        RCLCPP_DEBUG(this->get_logger(), "Instrucción añadida: %s", instruction.c_str());
                    }
                }
            }
        }

        // Configurar estado del robot si se recibió un plan válido
        if (!current_plan_.empty()) {
            current_plan_index_ = 0;
            instruction_counter_ = 0;
            has_active_plan_ = true;
            
            RCLCPP_INFO(this->get_logger(), "Plan procesado: %zu instrucciones recibidas", current_plan_.size());
            RCLCPP_INFO(this->get_logger(), "Plan listo para ejecución");
        } else {
            RCLCPP_WARN(this->get_logger(), "Plan recibido pero no se encontraron instrucciones válidas");
        }
    }

    // --------------------------
    // MANEJO DEL SERVICIO
    // --------------------------
    void handleInstructionRequest(
        const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
        std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
        
        (void)request;  // No se usa el request en este caso

        RCLCPP_INFO(this->get_logger(), "Solicitud de instrucción recibida");

        // Verificar si hay un plan activo
        if (!has_active_plan_ || current_plan_.empty()) {
            response->success = false;
            response->message = "NoPlanAssigned";
            RCLCPP_WARN(this->get_logger(), "No hay plan activo para ejecutar");
            return;
        }

        // Obtener la siguiente instrucción
        std::string instruction = current_plan_[instruction_counter_];
        
        // Incrementar contador para la próxima solicitud
        instruction_counter_++;
        
        // Publicar en el tópico (opcional)
        auto instruction_msg = std_msgs::msg::String();
        instruction_msg.data = instruction;
        publisher_->publish(instruction_msg);
        
        // Configurar respuesta del servicio
        response->success = true;
        response->message = instruction;
        
        RCLCPP_INFO(this->get_logger(), "Instrucción %d/%zu enviada: %s",
                   instruction_counter_, current_plan_.size(), instruction.c_str());
        
        // Verificar si se completó el plan
        if (instruction_counter_ >= (int)current_plan_.size()) {
            has_active_plan_ = false;
            RCLCPP_INFO(this->get_logger(), "Última instrucción del plan enviada");
        }
    }
    
};

// --------------------------
// MAIN
// --------------------------
int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<SupervisorPlanNode>();
    
    RCLCPP_INFO(node->get_logger(), "Plan Manager Node iniciado");
    RCLCPP_INFO(node->get_logger(), "Esperando planes en /clips/results");
    RCLCPP_INFO(node->get_logger(), "Servicio disponible en /request_instruction");
    
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}