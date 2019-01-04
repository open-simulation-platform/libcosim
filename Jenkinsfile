pipeline {
    agent none

    options { checkoutToSubdirectory('cse-core') }

    stages {

        stage('Build') {
            parallel {
                stage('Build on Windows') {
                    agent { label 'windows' }
                    
                    environment {
                        CONAN_USER_HOME = "${env.BASE}\\conan-repositories\\${env.EXECUTOR_NUMBER}"
                        CONAN_USER_HOME_SHORT = "${env.CONAN_USER_HOME}"
                        OSP_CONAN_CREDS = credentials('jenkins-osp-conan-creds')
                        CSE_CONAN_CHANNEL = "${env.BRANCH_NAME}".replaceAll("/", "_")
                    }

                    stages {
                        stage('Configure Conan') {
                            steps {
                                sh 'conan remote add osp https://osp-conan.azurewebsites.net/artifactory/api/conan/conan-local --force'
                                sh 'conan user -p $OSP_CONAN_CREDS_PSW -r osp $OSP_CONAN_CREDS_USR'
                            }
                        }
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'conan install ../cse-core -s build_type=Debug -b missing -o ci=True'
                                    bat 'conan build -c -b -pf package/windows/debug ../cse-core'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build') {
                                    bat 'conan install ../cse-core -s build_type=Release -b missing -o ci=True'
                                    bat 'conan build -c -b -pf package/windows/release ../cse-core'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'debug-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('debug-build') {
                                        sh "conan export-pkg ../cse-core osp/${CSE_CONAN_CHANNEL} --force"
                                        sh "conan upload cse-core/*@osp/${CSE_CONAN_CHANNEL} --all -r=osp --confirm"
                                    }
                                    dir('debug-build/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: true
                                    }
                                }
                                cleanup {
                                    dir('debug-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    bat 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'release-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('release-build') {
                                        sh "conan export-pkg ../cse-core osp/${CSE_CONAN_CHANNEL} --force"
                                        sh "conan upload cse-core/*@osp/${CSE_CONAN_CHANNEL} --all -r=osp --confirm"    
                                    }
                                    dir('release-build/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: true
                                    }
                                }
                                cleanup {
                                    dir('release-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                    }
                }
                stage ( 'Build on Linux with Conan' ) {
                    agent { 
                        dockerfile {
                            filename 'Dockerfile.conan-build'
                            dir 'cse-core/.dockerfiles'
                            label 'linux && docker'
                            args '-v ${HOME}/jenkins_slave/conan-repositories/${EXECUTOR_NUMBER}:/conan_repo'
                        }
                    }

                    environment {
                        CONAN_USER_HOME = '/conan_repo'
                        CONAN_USER_HOME_SHORT = 'None'
                    }
                    
                    stages {
                        stage('Build Debug') {
                            steps {
                                dir('debug-build-conan') {
                                    sh 'conan install ../cse-core -s compiler.libcxx=libstdc++11 -s build_type=Debug -b missing'
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install/linux-conan/debug -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build-conan') {
                                    sh 'conan install ../cse-core -s compiler.libcxx=libstdc++11 -s build_type=Release -b missing'
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install/linux-conan/release -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build-conan') {
                                    sh '. ./activate_run.sh && ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build-conan') {
                                    sh '. ./activate_run.sh && ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                    }
                    post {
                        always{
                            xunit (
                                testTimeMargin: '30000',
                                thresholdMode: 1,
                                thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                tools: [ CTest(pattern: '*-build-conan/Testing/**/Test.xml') ]
                            )
                        }
                        success {
                            dir('install') {
                                archiveArtifacts artifacts: '**',  fingerprint: true
                            }
                        }
                        cleanup {
                            dir('debug-build-conan/Testing') {
                                deleteDir();
                            }
                            dir('release-build-conan/Testing') {
                                deleteDir();
                            }
                        }
                    }
                }
                stage ( 'Build on Linux with Docker' ) {
                    agent { 
                        dockerfile { 
                            filename 'Dockerfile.build'
                            dir 'cse-core/.dockerfiles'
                            label 'linux && docker'
                        }
                    }

                    stages {
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install/linux/debug -DCSECORE_USING_CONAN=FALSE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build ') {
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install/linux/release -DCSECORE_USING_CONAN=FALSE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    sh 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                    }
                    post {
                        always{
                            xunit (
                                testTimeMargin: '30000',
                                thresholdMode: 1,
                                thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                tools: [ CTest(pattern: '*-build/Testing/**/Test.xml') ]
                            )
                        }
                        success {
                            dir('install') {
                                archiveArtifacts artifacts: '**',  fingerprint: true
                            }
                        }
                        cleanup {
                            dir('debug-build/Testing') {
                                deleteDir();
                            }
                            dir('release-build/Testing') {
                                deleteDir();
                            }
                        }
                    }
                }
            }
        }
    }
}
