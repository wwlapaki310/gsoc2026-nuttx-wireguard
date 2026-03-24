# Proposal for Community of Code Glasgow, ASF Conference 2026
## Submission Title 
Implementing WireGuard for Apache NuttX: A GSoC Journey into Secure, High-Performance Edge Connectivity 

## Brief description 
This session explores the implementation of WireGuard—a modern, lightweight, high-performance VPN protocol—on Apache NuttX RTOS as part of Google Summer of Code (GSoC) 2026. I will discuss the technical process of enabling secure, low-latency connectivity in a resource-constrained environment.

## Spreaker biography
Satoru Akita is an AI System Engineer based in Japan, currently with Sony Semiconductor Solutions Corporation. Over the past seven years, he has focused on the social implementation of the edge AI platform "AITRIOS," leading open-source initiatives through his professional work, including the public release of AI camera sample applications on GitHub. He holds a Master’s degree in Robotics from Tohoku University, where he conducted research on MEMS and autonomous systems. Driven by a desire to contribute more deeply to existing OSS ecosystems, Satoru is currently a GSoC 2026 applicant for the Apache NuttX project. His goal is to implement the WireGuard VPN protocol for platforms like SPRESENSE and ESP32, aiming to build secure, low-latency communication infrastructure for edge AI cameras and satellite projects. Through these efforts, he strives to enable "Sense the Wonder" by connecting the world through secure and innovative technology. In addition to his engineering work, Satoru is an active supporter of the technology community. He serves as a volunteer staff member for Open Source Summit Japan and SRE Next, and as an alumni advisor for the SecHack365 security talent program. He leverages his enterprise experience to bridge the gap between industrial AI implementation and community-driven open-source development.

## Full abstract
[Introduction]

Apache NuttX is a POSIX-compliant RTOS widely used in resource-constrained environments. However, a significant gap exists in its networking stack: the lack of a native VPN capability. As devices are increasingly deployed in remote or untrusted networks, the need for secure, encrypted tunnels for diagnostics and maintenance has become a critical requirement.


[About WireGuard]

To address this, as part of Google Summer of Code (GSoC) 2026, I undertook the porting of WireGuard to Apache NuttX. WireGuard is an extremely simple yet fast and modern VPN that utilizes state-of-the-art cryptography. Designed to be leaner and more performant than IPsec or OpenVPN, it aims to be as easy to configure as SSH. Its ability to roam between IP addresses and its minimal codebase make it ideal for the embedded interfaces provided by NuttX.



[Technical Roadmap & Implementation]

This session covers the architectural journey of implementing WireGuard as a standard NuttX network device. Following my project roadmap, I will discuss:



The Porting Strategy: Establishing the foundational cryptographic primitives and integrating them into the NuttX build system.



Network Device Integration: The technical approach to mapping WireGuard’s "Cryptokey Routing" to the NuttX BSD socket interface.



Feasibility & Challenges: Early insights from the implementation phase, focusing on the trade-offs required for resource-constrained RTOS environments.



[Community Journey: A Professional's First Steps in OSS]

Beyond the code, I will share my personal growth story. Despite seven years of professional experience in the industry, this was my first major foray into open-source contribution. I will discuss the challenges of entering the Apache ecosystem, the learning curve of GSoC, and how an "OSS novice" can navigate the community's expectations to deliver core functionality. This talk provides a practical guide for experienced engineers who want to start their own journey into the world of open source.


# Link
https://communityovercode.org/
https://communityovercode.org/call-for-presentations/
https://www.cvent.com/c/abstracts/ecaa843e-109e-4f2a-b359-3a442542e4ba
