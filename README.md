# BE Capstone Project

## Project Title

**Advanced SLI**

---

## Team Details

| Sr. No. | Name of Student | Roll No. | Branch | Email ID |
|---|---|---|---|---|
| 1 | Mohammed Salah Altaf Chogle | 41 | Automation & Robotics | 2023.mohammed.chogle@ves.ac.in |
| 2 | Ali Kalsekar | 53 | Automation & Robotics | d2024.ali.kalsekar@ves.ac.in |
| 3 | Aayush Kadam | 51 | Automation & Robotics | 2023.aayush.kadam@ves.ac.in |


---

## Guide Details

**Project Guide:** Gopalkrishnan Narayanan 
**Department:** Automation and Robotics  
**Institute:** VESIT, Mumbai  

---

## Problem Statement

> The aim of this project is to design and develop an affordable, retrofittable Safe Load Indicator (SLI) system for low-capacity truck-mounted pick-and-carry mobile cranes by using multi-sensor feedback (load, angle, extension, inertial data) and embedded control technology, addressing the lack of accurate, low-cost load-monitoring and safety systems available for this crane category.

---

## Abstract

Low-capacity truck-mounted pick-and-carry hydraulic cranes, commonly used at local worksites, largely lack accurate and affordable Safe Load Indicator (SLI) systems, unlike larger construction-grade cranes which have factory-fitted rated capacity indicators. Operators of these smaller cranes often rely on experience alone, increasing the risk of overloading and tipping incidents. This project proposes the design and development of an Advanced Intelligent Safe Load Indicator (AI-SLI): a sensor-driven system that monitors boom angle, extension, hydraulic pressure (as a proxy for load), and inertial motion data to provide real-time load percentage, safety alarms, and operational analytics.

The project is being developed in two parallel tracks: a custom-built scale-model truck-mounted crane platform (with independently actuated swing, telescopic extension, boom-lift, and cable-reeling axes, each with encoder and sensor feedback) used for mechanism validation and algorithm development, and eventual sensor integration on a real crane for field validation. The scale model itself is a standalone multi-axis robotic-arm project, combining mechanical design, embedded systems, and sensor fusion.

Software development follows a phased approach: starting with basic rated-capacity load-chart safety limits, followed by PID-based anti-sway control, tipping prediction using load and outrigger data, and eventually AI-based analytics such as inertial-data-driven operator scoring. The expected outcome is a working scale-model demonstrator validating the sensing and control approach, laying the foundation for deployment on a real crane.

---

## Objectives

1. To study existing Safe Load Indicator systems and identify gaps in affordability and availability for low-capacity truck-mounted cranes.
2. To design and build a multi-axis scale-model crane (swing, telescopic extension, boom lift, cable reeling) as a development and validation platform.
3. To design a sensor and control architecture capable of measuring load, angle, extension, and inertial motion.
4. To implement basic rated-capacity load-chart safety limit functionality.
5. To progressively implement advanced features: anti-sway control (PID) and tipping prediction.
6. To validate the developed system and, in later stages, apply learnings toward real crane sensor integration.

---

## Scope of the Project

- Design and fabrication of a multi-axis scale-model truck-mounted crane (mechanical + electrical)
- Sensor integration: load cell, IMU, magnetic encoders, limit switches
- Embedded firmware for motor control and sensor data acquisition
- Desktop dashboard application for live monitoring, data logging, and analytics
- Phased safety software: load-chart limits → anti-sway control → tipping prediction
- Data collection and testing on the scale model
- Preliminary exploration of real crane sensor integration (later phase, scope to be expanded as work progresses)

---

## Existing System

Large construction-grade cranes typically come with factory-fitted Rated Capacity Indicator (RCI) / SLI systems. However, small, local, truck-mounted pick-and-carry hydraulic cranes commonly used at smaller worksites usually operate without such systems.

Limitations of the current situation:

- High cost of commercial SLI systems relative to the value of low-capacity cranes
- Lack of retrofittable options for older or lower-capacity crane models
- Operators rely on experience/judgement rather than real-time load feedback
- No predictive safety features (e.g., sway or tipping warnings) on this crane category
- Limited accessibility of safety technology for smaller crane operators

---

## Proposed System

**Main idea:** An affordable, sensor-driven Safe Load Indicator system, developed and validated on a custom-built scale-model crane before targeting real crane deployment.

**How it works:** Boom angle, extension length, and load/pressure are continuously measured and combined (via a load-chart lookup) to compute real-time load percentage and trigger safety alarms. Inertial (IMU) data is used to detect motion events, assist with anti-sway damping, and support operator analytics. Data is transmitted to a desktop dashboard for live monitoring and logging.

**Major components:** Multi-axis scale-model crane (swing, telescopic extension, boom lift, cable reeling), magnetic encoders, IMU, load cell, microcontroller-based control system, and a PyQt6 desktop dashboard.

**Expected benefits:** Real-time load awareness, improved operator safety, a low-cost architecture suited to smaller cranes, and a foundation for future predictive safety features.

**Video** https://www.youtube.com/watch?v=6nNT_Is4-v4

---

## System Architecture

Add block diagram or system architecture image here.

````markdown
![System Architecture](images/system_architecture.png)
````

Briefly explain the architecture.

---

## Hardware Requirements

| Sr. No. | Component | Specification | Quantity | Purpose |
| ------- | --------- | ------------- | -------- | ------- |
| 1       | NEMA17 Stepper Motor | 17HS8401 | 3 | Swing, boom lift, telescopic extension actuation |
| 2       | N20 DC Gear Motor | 150 RPM, encoder-integrated | 1 | Cable reeling / winch drive |
| 3       | Arduino Mega 2560 | — | 1 | Main controller |
| 4       | RAMPS 1.4 | — | 1 | Motor driver / sensor breakout shield |
| 5       | A4988 Stepper Driver | — | 4 | Stepper motor driving |
| 6       | AS5600 Magnetic Encoder | — | 2 | Swing and boom lift angle feedback |
| 7       | MPU6050 IMU | 6-axis | 1 | Inertial motion sensing |
| 8       | HX711 + Load Cell | 5 kg | 1 | Load sensing |
| 9       | Limit Switches | Mechanical | As required | Axis end-stop safety |
| 10      | 12V Power Supply | 12V, 10A | 1 | System power |

---

## Software Requirements

| Sr. No. | Software / Tool | Version | Purpose |
| ------- | --------------- | ------- | ------- |
| 1       | Arduino IDE     | —       | Firmware development |
| 2       | Fusion 360      | —       | Mechanical CAD design |
| 3       | Python          | —       | Data processing, dashboard, model training |
| 4       | PyQt6           | —       | Desktop dashboard application |

---

## Technologies Used

* Embedded C++ (Arduino)
* Python (data processing, dashboard, model training)
* Arduino Mega, RAMPS 1.4, A4988 stepper drivers
* Sensor fusion (Madgwick / Kalman filtering for IMU data)
* Machine Learning (LSTM — planned for future tipping prediction phase)
* PyQt6 (desktop dashboard)
* CAD Design (Fusion 360)

---

## Methodology

Explain the step-by-step approach.

1. Literature survey
2. Problem identification
3. Requirement analysis
4. System design
5. Hardware/software development
6. Integration
7. Testing and validation
8. Documentation and publication

---

## Project Timeline

| Week / Month | Task Planned          | Status                            |
| ------------ | --------------------- | --------------------------------- |
| Week 1       | Problem finalization  | Completed
| Week 2       | Literature survey     | Completed                                  |
| Week 3       | Requirement analysis  | Completed                                  |
| Week 4       | System design         | Completed                                  |
| Week 5       | Prototype development | In Progress                                  |
| Week 6       | Testing               | Pending                                  |
| Week 7       | Documentation         | Pending                                  |
| Week 8       | Paper writing         | Pending                                  |

---

## Weekly Progress Updates

Students must update this section every week.

| Week   | Date | Work Completed | Work Planned for Next Week | Issues / Challenges | GitHub Commit Link |
| ------ | ---- | -------------- | -------------------------- | ------------------- | ------------------ |
| Week 1 | Feb 2026     | Finalized problem statement               |    Begin literature survey	                        |                     |                    |
| Week 2 | Mar 2026     | Literature survey               | Incorporate review feedback, finalize scope                           |                     |                    |
| Week 3 | Apr 2026     | Incorporated review feedback/modifications; finalized scope for submission               | Requirement analysis & component selection                           |                     |                    |
| Week 4 | Jul 2026     | Requirement analysis & component selection               |                            |                     |                    |
| Week 5 | Jul 2026     | Researched mechanical systems for prototype design               |                            |                     |                    |
| Week 6 | Jul 2026     | Mechanical design & 3D CAD modelling               |                            |                     |                    |
| Week 7 | Aug 2026     | 3D printing of mechanical parts and assembly               |                            |                     |                    |
| Week 8 | Aug 2026     | Prototype PCB and electronic configuration               |                            |                     |                    |
| Week 8 | Aug 2026     |              |                            |                     |                    |
| Week 8 | Aug 2026     |               |                            |                     |                    |
| Week 8 | Aug 2026     |                |                            |                     |                    |
---

## Design Files

Upload and link all design files here.

| File Type       | File Name / Link | Description |
| --------------- | ---------------- | ----------- |
| CAD Model       |                  |             |
| Circuit Diagram |                  |             |
| PCB Design      |                  |             |
| Flowchart       |                  |             |
| Simulation File |                  |             |

---

## Circuit Diagram

Add circuit diagram image here.

````markdown
![Circuit Diagram](images/circuit_diagram.png)
````

---

## Flowchart / Algorithm

Add flowchart image here.

````markdown
![Flowchart](images/flowchart.png)
````

### Algorithm

1. Start
2. Initialize the system
3. Read input from sensors/user
4. Process the data
5. Generate output/control action
6. Display/store/transmit result
7. Stop

---

## Implementation Details

Explain the actual implementation of the project.

### Hardware Implementation

Write details about connections, components, power supply, sensors, actuators, PCB, enclosure, etc.

### Software Implementation

Write details about code structure, libraries used, algorithms, communication protocols, database, app, cloud, etc.

---

## Code Structure

````text
BE-Capstone-Project/
│
├── README.md
├── docs/
│   ├── literature_survey.md
│   ├── project_report.pdf
│   └── presentation.pptx
│
├── hardware/
│   ├── circuit_diagram.png
│   ├── pcb_design/
│   └── cad_model/
│
├── software/
│   ├── src/
│   ├── include/
│   └── tests/
│
├── images/
│   ├── system_architecture.png
│   ├── prototype_photo.jpg
│   └── results.png
│
└── references/
    └── papers/
````

---

## How to Run the Project

### Step 1: Clone the Repository

````bash
git clone https://github.com/username/project-name.git
````

### Step 2: Install Dependencies

````bash
pip install -r requirements.txt
````

or mention specific software/library installation steps.

### Step 3: Upload / Run the Code

````bash
python main.py
````

or

````bash
arduino-cli upload -p COMx --fqbn board_name
````

### Step 4: Observe the Output

Mention the expected output of the project.

---

## Testing and Results

| Test No. | Test Description | Expected Result | Actual Result | Status      |
| -------- | ---------------- | --------------- | ------------- | ----------- |
| 1        |                  |                 |               | Pass / Fail |
| 2        |                  |                 |               | Pass / Fail |
| 3        |                  |                 |               | Pass / Fail |

---

## Result Images / Videos

Add images or videos of the working prototype.

````markdown
![Prototype](images/prototype_photo.jpg)
````

Video Link:

````markdown
[Project Demo Video](https://drive.google.com/your-video-link)
````

---

## Applications

Mention real-world applications of the project.

1.
2.
3.
4.

---

## Advantages

1.
2.
3.
4.

---

## Limitations

1.
2.
3.
4.

---

## Future Scope

Mention possible improvements.

1.
2.
3.
4.

---

## Research Paper / Publication

| Item                      | Details                                                   |
| ------------------------- | --------------------------------------------------------- |
| Paper Title               |                                                           |
| Conference / Journal Name |                                                           |
| Paper Status              | Not Started / Drafting / Submitted / Accepted / Published |
| Submission Date           |                                                           |
| Paper Link                |                                                           |

---

## References

Add references in IEEE format.

Example:

````text
[1] A. Author, B. Author, "Title of the Paper," Journal/Conference Name, vol. X, no. Y, pp. xx-yy, Year.
[2] Datasheet / Website / Book reference.
````

---

## Repository Update Guidelines

Each student team must update the GitHub repository regularly.

Minimum expected updates:

* Update README every week.
* Push code changes regularly.
* Upload circuit diagrams, CAD files, PCB files, reports and presentations.
* Add weekly progress in the progress table.
* Maintain proper folder structure.
* Do not upload unnecessary temporary files.
* Each major update should have a meaningful commit message.

Example commit messages:

````text
Added problem statement and objectives
Updated system architecture diagram
Added sensor interfacing code
Updated weekly progress for Week 3
Added testing results and prototype images
````

---

## Declaration

We declare that this project work is carried out by our team as part of the BE Capstone Project. The work will be regularly updated on GitHub and all references used will be properly cited.

---

## License

This project is for academic use only.

Optional:

````text
MIT License / Creative Commons / Institute Use Only
````

````
````
